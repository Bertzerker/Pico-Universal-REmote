/* Authenticated Android TV remote over Wi-Fi, served by raw lwIP. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "auth.h"
#include "tv_control.h"
#include "webpage.h"
#include "ir.h"
#include "ir_store.h"

typedef struct {
    char request[1600];
    unsigned received;
    unsigned pending;
    char *reply;
    unsigned reply_length;
    unsigned reply_sent;
} http_client_t;

static void detach(struct tcp_pcb *pcb) {
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
}

static err_t close_client(struct tcp_pcb *pcb, http_client_t *client) {
    detach(pcb);
    free(client->reply);
    free(client);
    err_t result = tcp_close(pcb);
    if (result != ERR_OK) tcp_abort(pcb);
    return result == ERR_OK ? ERR_OK : ERR_ABRT;
}

static void client_error(void *arg, err_t err) {
    (void)err;
    http_client_t *client = arg;
    free(client->reply);
    free(client);
}

static void pump_reply(struct tcp_pcb *pcb, http_client_t *client) {
    while (client->reply_sent < client->reply_length && tcp_sndbuf(pcb)) {
        unsigned remaining = client->reply_length - client->reply_sent;
        unsigned chunk = remaining < 512 ? remaining : 512;
        if (chunk > tcp_sndbuf(pcb)) chunk = tcp_sndbuf(pcb);
        if (tcp_write(pcb, client->reply + client->reply_sent,
                      chunk, TCP_WRITE_FLAG_COPY) != ERR_OK) break;
        client->reply_sent += chunk;
        client->pending += chunk;
    }
    tcp_output(pcb);
}

static err_t client_sent(void *arg, struct tcp_pcb *pcb, u16_t length) {
    http_client_t *client = arg;
    client->pending = length >= client->pending ? 0 : client->pending - length;
    pump_reply(pcb, client);
    if (!client->pending && client->reply_sent == client->reply_length)
        return close_client(pcb, client);
    return ERR_OK;
}

static err_t respond(struct tcp_pcb *pcb, http_client_t *client,
                     const char *status, const char *type, const void *body,
                     unsigned length, bool gzip_body) {
    char header[220];
    int head_length = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
        "Connection: close\r\n%s\r\n", status, type, length,
        gzip_body ? "Content-Encoding: gzip\r\n" : "");
    if (head_length < 0 || head_length >= sizeof(header) ||
            (unsigned)head_length > tcp_sndbuf(pcb))
        return close_client(pcb, client);
    client->reply = malloc(length ? length : 1);
    if (!client->reply) return close_client(pcb, client);
    memcpy(client->reply, body, length);
    client->reply_length = length;
    if (tcp_write(pcb, header, head_length, TCP_WRITE_FLAG_COPY) != ERR_OK)
        return close_client(pcb, client);
    client->pending = head_length;
    pump_reply(pcb, client);
    return ERR_OK;
}

static err_t json(struct tcp_pcb *pcb, http_client_t *client,
                  const char *status, const char *body) {
    return respond(pcb, client, status, "application/json", body, strlen(body), false);
}

static bool authorized(const char *headers, const char *end) {
    const char *line = strstr(headers, "\r\nAuthorization: Bearer ");
    if (!line || line >= end) return false;
    line += strlen("\r\nAuthorization: Bearer ");
    const char *line_end = strstr(line, "\r\n");
    return line_end && line_end <= end && auth_token_valid(line, line_end - line);
}

static bool target_matches(const char *headers, const char *end) {
    const char *line = strstr(headers, "\r\nX-Target: ");
    if (!line || line >= end) return false;
    line += strlen("\r\nX-Target: ");
    return line + 1 < end && line[0] == '0' + tv_target_get() &&
           line[1] == '\r' && line[2] == '\n';
}

static err_t process_request(struct tcp_pcb *pcb, http_client_t *client,
                             char *headers_end, unsigned body_length) {
    char method[8], path[64];
    if (sscanf(client->request, "%7s %63s", method, path) != 2)
        return json(pcb, client, "400 Bad Request", "{\"error\":\"Invalid request\"}");
    char *body = headers_end + 4;
    body[body_length] = 0;
    if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0)
        return respond(pcb, client, "200 OK", "text/plain",
                       "Pico Wi-Fi and Bluetooth running\n", 33, false);
    if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0)
        return respond(pcb, client, "200 OK", "text/html; charset=utf-8",
                       webpage_gz, webpage_gz_length, true);
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/login") == 0) {
        char token[33];
        if (!auth_login(body, body_length, token))
            return json(pcb, client, "401 Unauthorized", "{\"error\":\"Incorrect password\"}");
        char reply[64];
        snprintf(reply, sizeof(reply), "{\"token\":\"%s\"}", token);
        return json(pcb, client, "200 OK", reply);
    }
    if (!authorized(client->request, headers_end))
        return json(pcb, client, "401 Unauthorized", "{\"error\":\"Please log in\"}");
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/status") == 0) {
        char reply[64];
        snprintf(reply, sizeof(reply), "{\"connected\":%s,\"target\":%u}",
                 tv_connected() ? "true" : "false", tv_target_get());
        return json(pcb, client, "200 OK", reply);
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/bluetooth/target") == 0) {
        if (body_length != 1 || body[0] < '0' || body[0] > '1' ||
                !tv_target_select(body[0] - '0'))
            return json(pcb, client, "400 Bad Request", "{\"error\":\"Could not select Bluetooth target\"}");
        return json(pcb, client, "200 OK", "{\"ok\":true}");
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/ir/remotes") == 0)
        return json(pcb, client, "200 OK", ir_store_catalog());
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/ir/last") == 0)
        return json(pcb, client, "200 OK", ir_store_last_code());
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/ir/code") == 0) {
        unsigned remote, button;
        char extra;
        if (sscanf(body, "%u,%u%c", &remote, &button, &extra) != 2)
            return json(pcb, client, "400 Bad Request", "{\"error\":\"Invalid button\"}");
        const char *code = ir_store_code(remote, button);
        return code ? json(pcb, client, "200 OK", code) :
                      json(pcb, client, "404 Not Found", "{\"error\":\"Button not found\"}");
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/ir/send") == 0) {
        unsigned remote, button;
        char extra;
        if (sscanf(body, "%u,%u%c", &remote, &button, &extra) != 2 ||
                !ir_store_send(remote, button))
            return json(pcb, client, "400 Bad Request", "{\"error\":\"Unknown or unlearned IR button\"}");
        return json(pcb, client, "200 OK", "{\"ok\":true}");
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/ir/create") == 0) {
        unsigned rows, cols;
        char name[40], extra;
        if (sscanf(body, "%39[^\n]\n%u,%u%c", name, &rows, &cols, &extra) != 3 ||
                !ir_store_create(name, rows, cols))
            return json(pcb, client, "400 Bad Request", "{\"error\":\"Invalid remote or storage full\"}");
        return json(pcb, client, "200 OK", "{\"ok\":true}");
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/ir/rename") == 0) {
        unsigned remote;
        char name[40], extra;
        if (sscanf(body, "%u\n%39[^\n]%c", &remote, name, &extra) != 2 ||
                !ir_store_rename(remote, name))
            return json(pcb, client, "400 Bad Request", "{\"error\":\"Invalid name or storage full\"}");
        return json(pcb, client, "200 OK", "{\"ok\":true}");
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/ir/resize") == 0) {
        unsigned remote, rows, cols;
        char extra;
        if (sscanf(body, "%u,%u,%u%c", &remote, &rows, &cols, &extra) != 3 ||
                !ir_store_resize(remote, rows, cols))
            return json(pcb, client, "400 Bad Request", "{\"error\":\"Invalid grid or storage full\"}");
        return json(pcb, client, "200 OK", "{\"ok\":true}");
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/ir/button") == 0) {
        unsigned remote, button;
        char name[40], color[10], extra;
        if (sscanf(body, "%u,%u\n%39[^\n]\n%9s%c", &remote, &button, name, color, &extra) != 4 ||
                !ir_store_button(remote, button, name, color))
            return json(pcb, client, "400 Bad Request", "{\"error\":\"Invalid button or storage full\"}");
        return json(pcb, client, "200 OK", "{\"ok\":true}");
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/ir/delete") == 0) {
        unsigned remote;
        char extra;
        if (sscanf(body, "%u%c", &remote, &extra) != 1 || !ir_store_delete(remote))
            return json(pcb, client, "400 Bad Request", "{\"error\":\"Unknown remote or storage full\"}");
        return json(pcb, client, "200 OK", "{\"ok\":true}");
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/ir/learn") == 0) {
        unsigned remote, button;
        char extra;
        if (sscanf(body, "%u,%u%c", &remote, &button, &extra) != 2 ||
                !ir_store_learn_start(remote, button))
            return json(pcb, client, "409 Conflict", "{\"error\":\"Cannot start learning\"}");
        return json(pcb, client, "200 OK", "{\"ok\":true}");
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/ir/learn") == 0) {
        unsigned timings = 0;
        int status = ir_store_learn_status(&timings);
        char reply[80];
        snprintf(reply, sizeof(reply), "{\"status\":%d,\"timings\":%u}", status, timings);
        return json(pcb, client, "200 OK", reply);
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/key") == 0) {
        if (!target_matches(client->request, headers_end))
            return json(pcb, client, "409 Conflict", "{\"error\":\"Remote switched; reopen the selected remote\"}");
        if (body_length > 40 || !tv_key(body))
            return json(pcb, client, "409 Conflict", "{\"error\":\"TV disconnected, busy, or unknown key\"}");
        return json(pcb, client, "200 OK", "{\"ok\":true}");
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/text") == 0) {
        if (!target_matches(client->request, headers_end))
            return json(pcb, client, "409 Conflict", "{\"error\":\"Remote switched; reopen the selected remote\"}");
        if (!tv_text(body, body_length))
            return json(pcb, client, "409 Conflict", "{\"error\":\"TV disconnected, busy, or unsupported text\"}");
        return json(pcb, client, "200 OK", "{\"ok\":true}");
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/move") == 0) {
        if (!target_matches(client->request, headers_end))
            return json(pcb, client, "409 Conflict", "{\"error\":\"Remote switched; reopen the selected remote\"}");
        int dx, dy;
        char extra;
        if (sscanf(body, "%d,%d%c", &dx, &dy, &extra) != 2 ||
                !tv_mouse_move(dx, dy))
            return json(pcb, client, "409 Conflict", "{\"error\":\"TV disconnected, busy, or invalid move\"}");
        return json(pcb, client, "200 OK", "{\"ok\":true}");
    }
    return json(pcb, client, "404 Not Found", "{\"error\":\"Not found\"}");
}

static err_t client_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *packet, err_t err) {
    http_client_t *client = arg;
    if (!packet || err != ERR_OK) {
        if (packet) pbuf_free(packet);
        return close_client(pcb, client);
    }
    if (client->pending) {
        tcp_recved(pcb, packet->tot_len);
        pbuf_free(packet);
        return ERR_OK;
    }
    unsigned available = sizeof(client->request) - 1 - client->received;
    unsigned copied = pbuf_copy_partial(packet, client->request + client->received,
                                        packet->tot_len < available ? packet->tot_len : available, 0);
    client->received += copied;
    tcp_recved(pcb, packet->tot_len);
    pbuf_free(packet);
    client->request[client->received] = 0;
    char *headers_end = strstr(client->request, "\r\n\r\n");
    if (!headers_end && available == copied) return close_client(pcb, client);
    if (!headers_end) return ERR_OK;
    unsigned header_length = headers_end + 4 - client->request;
    unsigned body_length = 0;
    const char *length_header = strstr(client->request, "\r\nContent-Length: ");
    if (length_header && length_header < headers_end)
        body_length = strtoul(length_header + strlen("\r\nContent-Length: "), NULL, 10);
    if (body_length > 128 || header_length + body_length >= sizeof(client->request))
        return json(pcb, client, "413 Payload Too Large", "{\"error\":\"Request too large\"}");
    if (client->received < header_length + body_length) return ERR_OK;
    return process_request(pcb, client, headers_end, body_length);
}

static err_t accept_client(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !pcb) return ERR_VAL;
    http_client_t *client = calloc(1, sizeof(*client));
    if (!client) { tcp_abort(pcb); return ERR_ABRT; }
    tcp_arg(pcb, client);
    tcp_recv(pcb, client_recv);
    tcp_sent(pcb, client_sent);
    tcp_err(pcb, client_error);
    return ERR_OK;
}

bool http_server_start(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return false;
    cyw43_arch_lwip_begin();
    err_t status = tcp_bind(pcb, NULL, 80);
    if (status == ERR_OK) {
        struct tcp_pcb *listener = tcp_listen_with_backlog(pcb, 2);
        if (listener) {
            tcp_accept(listener, accept_client);
            cyw43_arch_lwip_end();
            return true;
        }
    }
    tcp_close(pcb);
    cyw43_arch_lwip_end();
    return false;
}
