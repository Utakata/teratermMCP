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
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(5.0)
            s.connect((TTXMCP_HOST, TTXMCP_PORT))
            s.sendall((json.dumps(payload) + "\n").encode('utf-8'))

            data = s.recv(4096)
            if data:
                return json.loads(data.decode('utf-8'))
            return {"status": "error", "message": "No response from plugin"}
    except ConnectionRefusedError:
        return {"status": "error", "message": "Could not connect to Tera Term. Is Tera Term running with the TTXMCP plugin installed?"}
    except Exception as e:
        return {"status": "error", "message": f"Socket error: {str(e)}"}

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
