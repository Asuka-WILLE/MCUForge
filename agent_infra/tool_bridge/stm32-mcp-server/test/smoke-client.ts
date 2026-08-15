import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";

const token = process.env.MCUFORGE_BRIDGE_TOKEN ?? "";
const url = process.env.MCUFORGE_BRIDGE_URL ?? "http://127.0.0.1:8765/mcp";

if (token.length < 32) throw new Error("MCUFORGE_BRIDGE_TOKEN is required for the smoke client.");

const client = new Client({ name: "stm32-tool-bridge-smoke-client", version: "0.1.0" });
const transport = new StreamableHTTPClientTransport(new URL(url), {
  requestInit: { headers: { Authorization: `Bearer ${token}` } }
});

try {
  await client.connect(transport);
  const listed = await client.listTools();
  const snapshot = await client.callTool({ name: "stm32_get_project_snapshot", arguments: {} });
  const integrity = await client.callTool({ name: "stm32_verify_test_integrity", arguments: {} });
  console.log(JSON.stringify({
    tool_names: listed.tools.map(tool => tool.name),
    snapshot: snapshot.structuredContent,
    integrity: integrity.structuredContent
  }, null, 2));
} finally {
  await client.close();
}
