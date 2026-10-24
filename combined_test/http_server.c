/* Small raw-lwIP HTTP server for the combined firmware. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

typedef struct {
    char request[1024];
    unsigned received;
    unsigned pending;
} http_client_t;

static void detach(struct tcp_pcb *pcb) {
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
}

static err_t close_client(struct tcp_pcb *pcb, http_client_t *client) {
    detach(pcb);
    free(client);
    err_t result = tcp_close(pcb);
    if (result != ERR_OK) tcp_abort(pcb);
    return result == ERR_OK ? ERR_OK : ERR_ABRT;
}

static void client_error(void *arg, err_t err) {
    (void)err;
    free(arg);
}

static err_t client_sent(void *arg, struct tcp_pcb *pcb, u16_t length) {
    http_client_t *client = arg;
    if (length >= client->pending) return close_client(pcb, client);
    client->pending -= length;
    return ERR_OK;
}

static err_t client_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *packet, err_t err) {
    http_client_t *client = arg;
    if (!packet || err != ERR_OK) {
        if (packet) pbuf_free(packet);
        return close_client(pcb, client);
    }
    unsigned available = sizeof(client->request) - 1 - client->received;
    unsigned copied = pbuf_copy_partial(packet, client->request + client->received,
                                        packet->tot_len < available ? packet->tot_len : available, 0);
    client->received += copied;
    tcp_recved(pcb, packet->tot_len);
    pbuf_free(packet);
    client->request[client->received] = 0;
    if (!strstr(client->request, "\r\n\r\n") && client->received < sizeof(client->request)-1)
        return ERR_OK;
    const char *body = "Not found\n";
    const char *status = "404 Not Found";
    if (strncmp(client->request, "GET /health ", 12) == 0) {
        body = "Pico Wi-Fi and Bluetooth running\n";
        status = "200 OK";
    }
    char response[256];
    int size = snprintf(response, sizeof(response),
        "HTTP/1.1 %s\r\nContent-Type: text/plain\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s",
        status, (unsigned)strlen(body), body);
    if (size < 0 || size >= sizeof(response)) return close_client(pcb, client);
    err_t write_status = tcp_write(pcb, response, size, TCP_WRITE_FLAG_COPY);
    if (write_status != ERR_OK) return close_client(pcb, client);
    client->pending = size;
    tcp_output(pcb);
    return ERR_OK;
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
