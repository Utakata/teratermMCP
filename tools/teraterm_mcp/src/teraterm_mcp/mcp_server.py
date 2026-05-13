import asyncio
import socket
import json
import logging
from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
logger = logging.getLogger("teraterm_mcp")

app = Server("teraterm-mcp")
TTXMCP_HOST = "127.0.0.1"
TTXMCP_PORT = 8001

def send_to_plugin(payload: dict) -> dict:
    """Send a JSON payload to the TTXMCP native plugin via TCP socket."""

    # Input validation: Prevent excessively large payloads or command injection at the Python layer
    if len(json.dumps(payload)) > 3500:
        return {"status": "error", "message": "Payload exceeds maximum allowed size for Tera Term injection."}

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(5.0) # 5 seconds timeout to prevent hanging the MCP server
            s.connect((TTXMCP_HOST, TTXMCP_PORT))

            # Send payload
            data_to_send = json.dumps(payload) + "\n"
            s.sendall(data_to_send.encode('utf-8'))

            # Receive response
            data = s.recv(4096)
            if not data:
                return {"status": "error", "message": "Connection closed by Tera Term plugin before responding."}

            try:
                return json.loads(data.decode('utf-8'))
            except json.JSONDecodeError as e:
                logger.error(f"Malformed JSON from plugin: {data}")
                return {"status": "error", "message": "Received malformed JSON response from Tera Term plugin."}

    except socket.timeout:
        return {"status": "error", "message": "Communication with Tera Term timed out. The process might be blocked or busy."}
    except ConnectionRefusedError:
        return {"status": "error", "message": "Could not connect to Tera Term. Is Tera Term running with the TTXMCP plugin installed?"}
    except ConnectionResetError:
        return {"status": "error", "message": "Connection was unexpectedly reset by Tera Term."}
    except Exception as e:
        return {"status": "error", "message": f"Unexpected Socket error: {str(e)}"}

@app.list_tools()
async def list_tools() -> list[Tool]:
    """List available Tera Term tools."""
    return [
        Tool(
            name="teraterm_connect",
            description="Inform the MCP to start monitoring the active Tera Term session.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="teraterm_read_buffer",
            description="Read the current output buffer from the active Tera Term session.",
            inputSchema={"type": "object", "properties": {}}
        ),
        Tool(
            name="teraterm_send_command",
            description="Inject a command into the active Tera Term session.",
            inputSchema={
                "type": "object",
                "properties": {
                    "command": {
                        "type": "string",
                        "description": "The command to execute"
                    }
                },
                "required": ["command"]
            }
        )
    ]

@app.call_tool()
async def call_tool(name: str, arguments: dict) -> list[TextContent]:
    """Handle tool execution requests by forwarding to the native C plugin."""

    # Input validation and sanitization
    if name == "teraterm_send_command":
        command = arguments.get("command", "")
        # Prevent binary injection or null bytes in commands sent to the terminal
        if "\x00" in command:
             return [TextContent(type="text", text="Error: Command contains forbidden null bytes.")]

    payload = {"action": name, "args": arguments}

    # Run socket communication in a thread pool to avoid blocking the async event loop
    result = await asyncio.to_thread(send_to_plugin, payload)

    if result.get("status") == "error":
        error_msg = result.get("message", "Unknown error")
        logger.error(f"Plugin error: {error_msg}")
        return [TextContent(type="text", text=f"Error: {error_msg}")]

    output = result.get("output", result.get("message", "Success"))
    return [TextContent(type="text", text=str(output))]

async def main_async():
    """Run the MCP server."""
    logger.info("Starting Tera Term MCP Server over stdio")
    async with stdio_server() as (read_stream, write_stream):
        await app.run(
            read_stream,
            write_stream,
            app.create_initialization_options()
        )

def main():
    asyncio.run(main_async())

if __name__ == "__main__":
    main()
