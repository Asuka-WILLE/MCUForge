import { createHash, timingSafeEqual } from "node:crypto";
import { promises as fs } from "node:fs";

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { createMcpExpressApp } from "@modelcontextprotocol/sdk/server/express.js";
import { hostHeaderValidation } from "@modelcontextprotocol/sdk/server/middleware/hostHeaderValidation.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import type { Request, Response } from "express";
import { z } from "zod/v4";

const DEFAULT_PORT = 8766;
const REQUEST_TIMEOUT_MS = 15_000;
const MAX_RESPONSE_BYTES = 1_000_000;
const DEFAULT_MAX_CHARS = 12_000;
const MAX_RETURN_CHARS = 16_000;
const MAX_REQUESTS_PER_MINUTE = 30;
const MCP_PATHS = ["/mcp", "/mcp-servers/mcp-research-web-bridge/mcp"];
const SOURCE_DOMAINS = [
  "github.com",
  "raw.githubusercontent.com",
  "gitee.com",
  "csdn.net",
  "st.com",
  "stmicroelectronics.com",
  "developer.arm.com",
  "arm.com"
] as const;
const SEARCH_SITE_SUFFIX: Record<SearchSite, string> = {
  any: "",
  official: "(site:st.com OR site:stmicroelectronics.com OR site:developer.arm.com OR site:arm.com)",
  github: "site:github.com",
  gitee: "site:gitee.com",
  csdn: "site:csdn.net"
};
const SEARCH_HOST = "www.bing.com";

type SearchSite = "any" | "official" | "github" | "gitee" | "csdn";

interface ResearchError {
  [key: string]: unknown;
  error: string;
  next_action: string;
}

interface SearchResult {
  title: string;
  url: string;
  summary: string;
  host: string;
}

interface FetchedText {
  finalUrl: string;
  contentType: string;
  body: string;
}

const researchToken = process.env.MCUFORGE_RESEARCH_TOKEN ?? "";
const consumerHashPath = process.env.MCUFORGE_RESEARCH_CONSUMER_HASH_PATH ?? "";
const port = Number.parseInt(process.env.MCUFORGE_RESEARCH_PORT ?? String(DEFAULT_PORT), 10);
const requestTimes: number[] = [];

function toolSuccess(value: Record<string, unknown>): CallToolResult {
  return {
    content: [{ type: "text", text: JSON.stringify(value, null, 2) }],
    structuredContent: value
  };
}

function toolError(error: unknown, nextAction: string): CallToolResult {
  const output: ResearchError = {
    error: error instanceof Error ? error.message : String(error),
    next_action: nextAction
  };
  return {
    content: [{ type: "text", text: JSON.stringify(output, null, 2) }],
    structuredContent: output,
    isError: true
  };
}

function isAllowedDomain(hostname: string): boolean {
  const normalized = hostname.toLowerCase().replace(/\.$/, "");
  return SOURCE_DOMAINS.some(domain => normalized === domain || normalized.endsWith(`.${domain}`));
}

function isSearchDomain(hostname: string): boolean {
  const normalized = hostname.toLowerCase().replace(/\.$/, "");
  return normalized === "bing.com" || normalized.endsWith(".bing.com");
}

function hostMatchesDomain(hostname: string, domain: string): boolean {
  const normalized = hostname.toLowerCase().replace(/\.$/, "");
  return normalized === domain || normalized.endsWith(`.${domain}`);
}

function matchesSearchSite(site: SearchSite, hostname: string): boolean {
  if (site === "any") return isAllowedDomain(hostname);
  if (site === "official") {
    return ["st.com", "stmicroelectronics.com", "developer.arm.com", "arm.com"]
      .some(domain => hostMatchesDomain(hostname, domain));
  }
  return hostMatchesDomain(hostname, `${site}.com`) || (site === "csdn" && hostMatchesDomain(hostname, "csdn.net"));
}

function validateSourceUrl(input: string, allowSearchDomain = false): URL {
  const url = new URL(input);
  if (url.protocol !== "https:") throw new Error("Only HTTPS source URLs are allowed.");
  if (url.username || url.password) throw new Error("Source URLs must not contain credentials.");
  if (!isAllowedDomain(url.hostname) && !(allowSearchDomain && isSearchDomain(url.hostname))) {
    throw new Error("Source host is not on the MCUForge public research allowlist.");
  }
  return url;
}

function enforceRateLimit(): void {
  const now = Date.now();
  while (requestTimes.length > 0 && requestTimes[0]! <= now - 60_000) requestTimes.shift();
  if (requestTimes.length >= MAX_REQUESTS_PER_MINUTE) {
    throw new Error(`Research request rate limit reached (${MAX_REQUESTS_PER_MINUTE}/minute).`);
  }
  requestTimes.push(now);
}

function decodeEntities(value: string): string {
  return value
    .replaceAll("&amp;", "&")
    .replaceAll("&lt;", "<")
    .replaceAll("&gt;", ">")
    .replaceAll("&quot;", "\"")
    .replaceAll("&#39;", "'")
    .replaceAll("&nbsp;", " ");
}

function removeMarkup(value: string): string {
  return decodeEntities(
    value
      .replace(/<!\[CDATA\[([\s\S]*?)\]\]>/g, "$1")
      .replace(/<script\b[^>]*>[\s\S]*?<\/script>/gi, " ")
      .replace(/<style\b[^>]*>[\s\S]*?<\/style>/gi, " ")
      .replace(/<[^>]+>/g, " ")
      .replace(/\s+/g, " ")
      .trim()
  );
}

function limitedText(value: string, maxChars: number): { text: string; truncated: boolean } {
  if (value.length <= maxChars) return { text: value, truncated: false };
  return { text: `${value.slice(0, maxChars)}\n...[truncated]`, truncated: true };
}

function isTextualContentType(contentType: string): boolean {
  const normalized = contentType.toLowerCase();
  return normalized.startsWith("text/") || normalized.includes("json") || normalized.includes("xml") || normalized.includes("javascript");
}

async function fetchPublicText(initialUrl: URL, allowSearchDomain = false): Promise<FetchedText> {
  let current = initialUrl;
  for (let redirects = 0; redirects <= 4; redirects += 1) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
    let response: globalThis.Response;
    try {
      response = await fetch(current, {
        redirect: "manual",
        signal: controller.signal,
        headers: {
          Accept: "text/html, text/plain, application/json, application/xml, text/markdown;q=0.9",
          "User-Agent": "MCUForgeResearchMCP/0.1 (read-only technical source retrieval)"
        }
      });
    } finally {
      clearTimeout(timer);
    }

    if ([301, 302, 303, 307, 308].includes(response.status)) {
      const location = response.headers.get("location");
      if (!location) throw new Error("Source returned a redirect without a Location header.");
      current = validateSourceUrl(new URL(location, current).toString(), allowSearchDomain);
      continue;
    }
    if (!response.ok) throw new Error(`Source request failed with HTTP ${response.status}.`);
    const contentType = response.headers.get("content-type") ?? "application/octet-stream";
    if (!isTextualContentType(contentType)) throw new Error("Source is not a supported text or HTML document.");
    const declaredLength = Number.parseInt(response.headers.get("content-length") ?? "0", 10);
    if (Number.isFinite(declaredLength) && declaredLength > MAX_RESPONSE_BYTES) {
      throw new Error(`Source exceeds the ${MAX_RESPONSE_BYTES}-byte download limit.`);
    }
    const bytes = Buffer.from(await response.arrayBuffer());
    if (bytes.length > MAX_RESPONSE_BYTES) throw new Error(`Source exceeds the ${MAX_RESPONSE_BYTES}-byte download limit.`);
    return { finalUrl: current.toString(), contentType, body: bytes.toString("utf8") };
  }
  throw new Error("Source exceeded the maximum redirect count.");
}

function rssField(item: string, field: string): string {
  const match = item.match(new RegExp(`<${field}>([\\s\\S]*?)<\\/${field}>`, "i"));
  return match ? removeMarkup(match[1] ?? "") : "";
}

function parseBingRss(xml: string): SearchResult[] {
  const results: SearchResult[] = [];
  for (const match of xml.matchAll(/<item>([\s\S]*?)<\/item>/gi)) {
    const item = match[1] ?? "";
    const title = rssField(item, "title");
    const rawUrl = rssField(item, "link");
    const summary = rssField(item, "description");
    if (!title || !rawUrl) continue;
    try {
      const url = validateSourceUrl(rawUrl);
      results.push({ title, url: url.toString(), summary, host: url.hostname });
    } catch {
      continue;
    }
  }
  return results;
}

function createServer(): McpServer {
  const server = new McpServer({ name: "research-web-mcp-server", version: "0.1.0" });

  server.registerTool(
    "research_get_policy",
    {
      title: "Get MCUForge Research Policy",
      description: "Return the fixed public-web research policy: allowed source domains, data limits, tool boundaries and citation fields. This performs no network request.",
      inputSchema: z.object({}).strict(),
      annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true, openWorldHint: false }
    },
    async () => toolSuccess({
      allowed_source_domains: SOURCE_DOMAINS,
      allowed_protocol: "https",
      search_provider: "Bing RSS",
      source_request_timeout_ms: REQUEST_TIMEOUT_MS,
      source_max_bytes: MAX_RESPONSE_BYTES,
      source_max_return_chars: MAX_RETURN_CHARS,
      request_rate_limit_per_minute: MAX_REQUESTS_PER_MINUTE,
      prohibited: ["downloads", "execution", "login", "cookies", "private-network access", "repository writes"],
      citation_fields: ["final_url", "retrieved_at_utc", "content_sha256", "specific_section_or_code_location", "license_conclusion"]
    })
  );

  server.registerTool(
    "research_search_sources",
    {
      title: "Search Allowed MCU Research Sources",
      description: "Search public technical sources through Bing RSS, then return only results on the MCUForge allowlist. Use site=official, github, gitee or csdn when source provenance matters. This is read-only and never logs in or downloads files.",
      inputSchema: z.object({
        query: z.string().min(2).max(200).describe("Technical search query, for example 'STM32H723 USB CDC receive callback'."),
        site: z.enum(["any", "official", "github", "gitee", "csdn"]).default("any").describe("Optional source class filter."),
        offset: z.number().int().min(0).max(9).default(0),
        limit: z.number().int().min(1).max(10).default(5)
      }).strict(),
      annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true, openWorldHint: true }
    },
    async ({ query, site, offset, limit }) => {
      try {
        enforceRateLimit();
        const scopedQuery = [query, SEARCH_SITE_SUFFIX[site]].filter(Boolean).join(" ");
        const searchUrl = new URL("https://www.bing.com/search");
        searchUrl.searchParams.set("format", "rss");
        searchUrl.searchParams.set("q", scopedQuery);
        const response = await fetchPublicText(searchUrl, true);
        const allResults = parseBingRss(response.body).filter(result => matchesSearchSite(site, result.host));
        const items = allResults.slice(offset, offset + limit);
        return toolSuccess({
          provider: "Bing RSS",
          query,
          site,
          search_url: searchUrl.toString(),
          total_allowed_results: allResults.length,
          count: items.length,
          offset,
          results: items,
          has_more: offset + items.length < allResults.length,
          next_offset: offset + items.length < allResults.length ? offset + items.length : null
        });
      } catch (error) {
        return toolError(error, "Use a more specific technical query or choose an allowed source class; if no authoritative source is available, ask the user for the manual.");
      }
    }
  );

  server.registerTool(
    "research_fetch_allowed_source",
    {
      title: "Fetch an Allowed Public Technical Source",
      description: "Fetch and normalize a public HTTPS page only from the MCUForge source allowlist. Returns final URL, retrieval time, full-content SHA-256 and bounded text for citation. Rejects credentials, non-text documents, private hosts, redirects outside the allowlist, downloads and pages over 1 MiB.",
      inputSchema: z.object({
        url: z.string().url().max(2_000).describe("HTTPS URL returned by research_search_sources or an allowed public source URL."),
        max_chars: z.number().int().min(500).max(MAX_RETURN_CHARS).default(DEFAULT_MAX_CHARS).describe("Maximum normalized text characters to return.")
      }).strict(),
      annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true, openWorldHint: true }
    },
    async ({ url, max_chars }) => {
      try {
        enforceRateLimit();
        const sourceUrl = validateSourceUrl(url);
        const response = await fetchPublicText(sourceUrl);
        const normalized = response.contentType.toLowerCase().includes("html") ? removeMarkup(response.body) : response.body.trim();
        const bounded = limitedText(normalized, max_chars);
        return toolSuccess({
          requested_url: sourceUrl.toString(),
          final_url: response.finalUrl,
          retrieved_at_utc: new Date().toISOString(),
          content_type: response.contentType,
          content_sha256: createHash("sha256").update(response.body, "utf8").digest("hex"),
          content_chars: normalized.length,
          content: bounded.text,
          truncated: bounded.truncated,
          citation_requirement: "Record final_url, retrieved_at_utc, content_sha256, exact section or code location, and license conclusion before reusing any fact or code."
        });
      } catch (error) {
        return toolError(error, "Use research_search_sources to obtain an allowed public URL. If the required source is blocked or absent, report blocked and ask the user for the manual.");
      }
    }
  );

  return server;
}

async function tokenMatches(authorization: string | undefined, bridgeHeader: string | undefined): Promise<boolean> {
  const candidate = bridgeHeader ?? (authorization?.startsWith("Bearer ") ? authorization.slice("Bearer ".length) : undefined);
  if (!candidate) return false;
  const supplied = Buffer.from(candidate);
  const expected = Buffer.from(researchToken);
  if (supplied.length === expected.length && timingSafeEqual(supplied, expected)) return true;
  if (!consumerHashPath) return false;
  try {
    const parsed = JSON.parse(await fs.readFile(consumerHashPath, "utf8")) as { token_sha256?: unknown };
    if (!Array.isArray(parsed.token_sha256)) return false;
    const candidateHash = Buffer.from(createHash("sha256").update(candidate).digest("hex"));
    return parsed.token_sha256.some(value => {
      if (typeof value !== "string" || !/^[0-9a-f]{64}$/i.test(value)) return false;
      const allowedHash = Buffer.from(value.toLowerCase());
      return allowedHash.length === candidateHash.length && timingSafeEqual(allowedHash, candidateHash);
    });
  } catch {
    return false;
  }
}

async function main(): Promise<void> {
  if (!Number.isInteger(port) || port < 1024 || port > 65_535) throw new Error("MCUFORGE_RESEARCH_PORT must be between 1024 and 65535.");
  if (researchToken.length < 32) throw new Error("MCUFORGE_RESEARCH_TOKEN must contain at least 32 characters.");

  const app = createMcpExpressApp({ host: "0.0.0.0" });
  app.use(hostHeaderValidation(["localhost", "127.0.0.1", "host.docker.internal", "aigw-local.hiclaw.io"]));
  app.get("/health", (_req: Request, res: Response) => {
    res.json({ status: "ok", service: "research-web-mcp-server" });
  });
  app.use(MCP_PATHS, async (req: Request, res: Response, next) => {
    if (!await tokenMatches(req.header("authorization"), req.header("x-mcuforge-research-token"))) {
      res.status(401).json({ error: "unauthorized" });
      return;
    }
    next();
  });
  app.post(MCP_PATHS, async (req: Request, res: Response) => {
    const server = createServer();
    const transport = new StreamableHTTPServerTransport({ sessionIdGenerator: undefined, enableJsonResponse: true });
    try {
      await server.connect(transport);
      await transport.handleRequest(req, res, req.body);
    } catch (error) {
      console.error("MCP request failed:", error);
      if (!res.headersSent) res.status(500).json({ jsonrpc: "2.0", error: { code: -32603, message: "Internal server error" }, id: null });
    } finally {
      await transport.close();
      await server.close();
    }
  });
  app.get(MCP_PATHS, (_req: Request, res: Response) => res.status(405).json({ error: "method_not_allowed" }));
  app.delete(MCP_PATHS, (_req: Request, res: Response) => res.status(405).json({ error: "method_not_allowed" }));

  app.listen(port, "0.0.0.0", () => {
    console.error(`Research Web Bridge listening on port ${port}`);
  });
}

main().catch(error => {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
});
