import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";

const token = process.env.MCUFORGE_RESEARCH_TOKEN ?? "";
const url = process.env.MCUFORGE_RESEARCH_URL ?? "http://127.0.0.1:8766/mcp";

if (token.length < 32) throw new Error("MCUFORGE_RESEARCH_TOKEN is required for the smoke client.");

const client = new Client({ name: "research-web-bridge-smoke-client", version: "0.1.0" });
const transport = new StreamableHTTPClientTransport(new URL(url), {
  requestInit: { headers: { Authorization: `Bearer ${token}` } }
});

try {
  await client.connect(transport);
  const listed = await client.listTools();
  const policy = await client.callTool({ name: "research_get_policy", arguments: {} });
  const blocked = await client.callTool({
    name: "research_fetch_allowed_source",
    arguments: { url: "http://127.0.0.1/private", max_chars: 500 }
  });
  if (!blocked.isError) throw new Error("Private HTTP URL was not rejected.");
  const fetched = await client.callTool({
    name: "research_fetch_allowed_source",
    arguments: { url: "https://github.com/agentscope-ai/AgentTeams", max_chars: 500 }
  });
  if (fetched.isError) throw new Error("Allowed GitHub source retrieval failed.");
  const searched = await client.callTool({
    name: "research_search_sources",
    arguments: { query: "STM32H723 USB CDC", site: "github", offset: 0, limit: 1 }
  });
  if (searched.isError) throw new Error(`Allowed GitHub search failed: ${JSON.stringify(searched.structuredContent)}`);
  console.log(JSON.stringify({
    tool_names: listed.tools.map(tool => tool.name),
    policy: policy.structuredContent,
    blocked_private_url: blocked.structuredContent,
    allowed_github_fetch: fetched.structuredContent,
    github_search: searched.structuredContent
  }, null, 2));
} finally {
  await client.close();
}
