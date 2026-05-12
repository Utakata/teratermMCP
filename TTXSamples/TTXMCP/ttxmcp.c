/*
 * TTXMCP - Tera Term MCP Native Plugin
 * Provides a local TCP server that allows Python/MCP agents to control Tera Term.
 */

#include "teraterm.h"
#include "tttypes.h"
#include "ttplugin.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <process.h>

#define ORDER 5000
#define MCP_PORT 8001

typedef struct {
  PTTSet ts;
  PComVar cv;
  SOCKET server_socket;
  HANDLE thread_handle;
  BOOL running;
  HWND hWin;
} TInstVar;

static TInstVar *pvar;
static TInstVar InstVar;

// Thread function for the TCP server
unsigned __stdcall ServerThread(void *arg) {
    TInstVar *var = (TInstVar *)arg;
    WSADATA wsaData;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);
    SOCKET client_socket;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return 1;
    }

    var->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (var->server_socket == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(MCP_PORT);

    if (bind(var->server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(var->server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(var->server_socket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(var->server_socket);
        WSACleanup();
        return 1;
    }

    while (var->running) {
        client_socket = accept(var->server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket == INVALID_SOCKET) {
            continue;
        }

        // Handle client connection (Simple single-threaded handler for now)
        char buffer[1024];
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';

            // In a full implementation, parse JSON here.
            // For now, if we receive a string, send it to Tera Term's CommSend.
            // This is a naive implementation; thread synchronization with the main UI thread is required for production.

            // Acknowledge
            const char* response = "{\"status\":\"ok\"}\n";
            send(client_socket, response, (int)strlen(response), 0);
        }
        closesocket(client_socket);
    }

    closesocket(var->server_socket);
    WSACleanup();
    return 0;
}


static void PASCAL TTXInit(PTTSet ts, PComVar cv) {
  pvar->ts = ts;
  pvar->cv = cv;
  pvar->running = TRUE;

  // Start TCP server thread
  unsigned thread_id;
  pvar->thread_handle = (HANDLE)_beginthreadex(NULL, 0, ServerThread, pvar, 0, &thread_id);
}

static void PASCAL TTXEnd(void) {
  pvar->running = FALSE;
  if (pvar->server_socket != INVALID_SOCKET) {
      closesocket(pvar->server_socket); // Force accept to unblock
  }
  if (pvar->thread_handle != NULL) {
      WaitForSingleObject(pvar->thread_handle, 1000);
      CloseHandle(pvar->thread_handle);
  }
}

static void PASCAL TTXGetUIHooks(TTXUIHooks *hooks) {
  // Store original hooks here if needed
}

static void PASCAL TTXSetWinSize(int rows, int cols) {
  // Can be used to track terminal geometry
}

static TTXExports Exports = {
  sizeof(TTXExports),
  ORDER,
  TTXInit,
  TTXGetUIHooks,
  NULL, // TTXGetSetupHooks
  NULL, // TTXOpenTCP
  NULL, // TTXCloseTCP
  TTXSetWinSize,
  NULL, // TTXModifyMenu
  NULL, // TTXModifyPopupMenu
  NULL, // TTXProcessCommand
  TTXEnd,
  NULL, // TTXSetCommandLine
  NULL, // TTXOpenFile
  NULL  // TTXCloseFile
};

BOOL __declspec(dllexport) PASCAL TTXBind(WORD Version, TTXExports *exports) {
  int size = sizeof(Exports) - sizeof(exports->size);

  if (size > exports->size) {
    size = exports->size;
  }
  memcpy((char *)exports + sizeof(exports->size),
         (char *)&Exports + sizeof(exports->size),
         size);

  pvar = &InstVar;
  memset(pvar, 0, sizeof(TInstVar));
  pvar->server_socket = INVALID_SOCKET;

  return TRUE;
}

// Note: In order to properly inject text into the Tera Term connection,
// we normally post a custom message to the main Tera Term window or call a specific API
// from the UI thread context.
// For now, the structural baseline is laid out.
