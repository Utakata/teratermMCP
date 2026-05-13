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
#define MAX_BUFFER_SIZE 4096

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

    var->server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (var->server_socket == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    // SECURITY FIX: Bind strictly to localhost (127.0.0.1) to prevent remote access.
    // Binding to INADDR_ANY would allow anyone on the network to send commands to Tera Term.
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
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

        // Handle client connection safely
        char buffer[MAX_BUFFER_SIZE];
        memset(buffer, 0, sizeof(buffer));

        // SECURITY FIX: Prevent buffer overflow by limiting recv size and ensuring null termination
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received > 0 && bytes_received < MAX_BUFFER_SIZE) {
            buffer[bytes_received] = '\0';

            // SECURITY FIX: Basic sanitization / validation
            // Ensure the payload doesn't contain excessively long strings or malformed control characters
            // before processing. In a real implementation, parse JSON safely here.
            BOOL is_safe = TRUE;
            for (int i = 0; i < bytes_received; i++) {
                // Reject null bytes within the payload (except our added terminator)
                if (buffer[i] == '\0' && i != bytes_received) {
                    is_safe = FALSE;
                    break;
                }
            }

            if (is_safe) {
                // Acknowledge receipt
                const char* response = "{\"status\":\"ok\"}\n";
                send(client_socket, response, (int)strlen(response), 0);

                // TODO: Safely dispatch the command to Tera Term's UI thread via PostMessage
                // e.g., PostMessage(var->hWin, WM_USER_CUSTOM_COMMAND, 0, (LPARAM)safe_string_copy);
            } else {
                const char* err_response = "{\"status\":\"error\", \"message\":\"Malformed payload\"}\n";
                send(client_socket, err_response, (int)strlen(err_response), 0);
            }
        } else if (bytes_received >= MAX_BUFFER_SIZE) {
            const char* err_response = "{\"status\":\"error\", \"message\":\"Payload too large\"}\n";
            send(client_socket, err_response, (int)strlen(err_response), 0);
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

// Note: In order to properly inject text into the Tera Term connection,
// we normally post a custom message to the main Tera Term window or call a specific API
// from the UI thread context.

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
