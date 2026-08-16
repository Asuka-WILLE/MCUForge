import { spawn } from "node:child_process";
import { createHash, timingSafeEqual } from "node:crypto";
import { promises as fs } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { createMcpExpressApp } from "@modelcontextprotocol/sdk/server/express.js";
import { hostHeaderValidation } from "@modelcontextprotocol/sdk/server/middleware/hostHeaderValidation.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import type { Request, Response } from "express";
import { z } from "zod/v4";

const CHARACTER_LIMIT = 25_000;
const FILE_SIZE_LIMIT = 256 * 1024;
const DEFAULT_PORT = 8765;
const MCP_PATHS = ["/mcp", "/mcp-servers/mcp-stm32-tool-bridge/mcp"];
const TEXT_EXTENSIONS = new Set([
  "", ".c", ".cc", ".cpp", ".csv", ".gitignore", ".h", ".hpp", ".ini", ".ioc",
  ".js", ".json", ".ld", ".md", ".ps1", ".py", ".s", ".sh", ".toml", ".ts",
  ".txt", ".uvprojx", ".xml", ".yaml", ".yml"
]);
const BLOCKED_SEGMENTS = new Set([".git", ".ssh", "node_modules"]);
const BLOCKED_FILE_PATTERNS = [/\.env(?:\.|$)/i, /secret/i, /credential/i, /hiclaw-manager\.env$/i];

interface CommandResult {
  command: string;
  exit_code: number;
  stdout: string;
  stderr: string;
  truncated: boolean;
  duration_ms: number;
}

interface ToolErrorShape {
  [key: string]: unknown;
  error: string;
  next_action: string;
}

const moduleDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(moduleDirectory, "../../../..");
const projectRoot = path.resolve(
  process.env.MCUFORGE_PROJECT_ROOT ?? path.join(repositoryRoot, "demos/vcw-board-demo/firmware")
);
const agentRoot = path.resolve(process.env.MCUFORGE_AGENT_ROOT ?? path.join(repositoryRoot, "agent_infra"));
const profileRoot = path.resolve(process.env.MCUFORGE_PROFILE_ROOT ?? path.join(repositoryRoot, "demos/vcw-board-demo/agent_profile"));
const bridgeToken = process.env.MCUFORGE_BRIDGE_TOKEN ?? "";
const consumerHashPath = process.env.MCUFORGE_CONSUMER_HASH_PATH ?? "";
const autonomousLocalMode = /^(1|true)$/i.test(process.env.MCUFORGE_AUTONOMOUS_LOCAL ?? "");
const port = Number.parseInt(process.env.MCUFORGE_BRIDGE_PORT ?? String(DEFAULT_PORT), 10);

function toolSuccess(value: Record<string, unknown>): CallToolResult {
  return {
    content: [{ type: "text", text: JSON.stringify(value, null, 2) }],
    structuredContent: value
  };
}

function toolError(error: unknown, nextAction: string): CallToolResult {
  const output: ToolErrorShape = {
    error: error instanceof Error ? error.message : String(error),
    next_action: nextAction
  };
  return {
    content: [{ type: "text", text: JSON.stringify(output, null, 2) }],
    structuredContent: output,
    isError: true
  };
}

function truncate(text: string): { text: string; truncated: boolean } {
  if (text.length <= CHARACTER_LIMIT) return { text, truncated: false };
  return {
    text: `${text.slice(0, CHARACTER_LIMIT)}\n...[truncated at ${CHARACTER_LIMIT} characters]`,
    truncated: true
  };
}

function resolveProjectPath(relativePath: string): string {
  const normalized = relativePath.replaceAll("\\", "/");
  if (path.isAbsolute(normalized)) throw new Error("Only project-relative paths are allowed.");
  const segments = normalized.split("/").filter(Boolean);
  if (segments.some(segment => BLOCKED_SEGMENTS.has(segment.toLowerCase()))) {
    throw new Error("The requested path contains a blocked directory.");
  }
  if (BLOCKED_FILE_PATTERNS.some(pattern => pattern.test(normalized))) {
    throw new Error("The requested path may contain credentials or secrets.");
  }
  const resolved = path.resolve(projectRoot, normalized);
  const relative = path.relative(projectRoot, resolved);
  if (relative.startsWith("..") || path.isAbsolute(relative)) {
    throw new Error("Path traversal outside the configured project root is not allowed.");
  }
  return resolved;
}

function resolveAgentPath(relativePath: string): string {
  const resolved = path.resolve(agentRoot, relativePath);
  const relative = path.relative(agentRoot, resolved);
  if (relative.startsWith("..") || path.isAbsolute(relative)) {
    throw new Error("Tool script path escaped the configured agent runtime root.");
  }
  return resolved;
}

function resolveProposalDirectory(proposalId: string): string {
  const proposalRoot = path.resolve(profileRoot, "patch_proposals");
  const resolved = path.resolve(proposalRoot, proposalId);
  const relative = path.relative(proposalRoot, resolved);
  if (!relative || relative.startsWith("..") || path.isAbsolute(relative)) {
    throw new Error("Patch proposal path escaped the configured profile root.");
  }
  return resolved;
}

function parseJsonResult(stdout: string): Record<string, unknown> {
  const parsed: unknown = JSON.parse(stdout);
  if (typeof parsed !== "object" || parsed === null || Array.isArray(parsed)) {
    throw new Error("Patch-channel script did not return a JSON object.");
  }
  return parsed as Record<string, unknown>;
}

async function withTemporaryPatch<T>(patchText: string, callback: (patchFile: string) => Promise<T>): Promise<T> {
  const temporaryDirectory = await fs.mkdtemp(path.join(tmpdir(), "mcuforge-patch-"));
  const patchFile = path.join(temporaryDirectory, "proposal.patch");
  try {
    await fs.writeFile(patchFile, patchText, "utf8");
    return await callback(patchFile);
  } finally {
    try { await fs.unlink(patchFile); } catch { /* best-effort cleanup of one generated file */ }
    try { await fs.rm(temporaryDirectory, { force: true }); } catch { /* leave no project files behind */ }
  }
}

async function runCommand(command: string, args: string[], timeoutMs: number): Promise<CommandResult> {
  const started = Date.now();
  return await new Promise<CommandResult>((resolve, reject) => {
    const child = spawn(command, args, {
      cwd: projectRoot,
      windowsHide: true,
      shell: false,
      env: process.env
    });
    let stdout = "";
    let stderr = "";
    let timedOut = false;
    const timer = setTimeout(() => {
      timedOut = true;
      child.kill();
    }, timeoutMs);
    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", chunk => { stdout += String(chunk); });
    child.stderr.on("data", chunk => { stderr += String(chunk); });
    child.on("error", error => {
      clearTimeout(timer);
      reject(error);
    });
    child.on("close", code => {
      clearTimeout(timer);
      if (timedOut) {
        reject(new Error(`Allowed command exceeded its ${timeoutMs} ms timeout.`));
        return;
      }
      const clippedOut = truncate(stdout.trim());
      const clippedErr = truncate(stderr.trim());
      resolve({
        command: [command, ...args].join(" "),
        exit_code: code ?? -1,
        stdout: clippedOut.text,
        stderr: clippedErr.text,
        truncated: clippedOut.truncated || clippedErr.truncated,
        duration_ms: Date.now() - started
      });
    });
  });
}

async function git(args: string[], timeoutMs = 30_000): Promise<CommandResult> {
  return await runCommand("git", ["-C", projectRoot, ...args], timeoutMs);
}

function createServer(): McpServer {
  const server = new McpServer({ name: "stm32-tool-bridge-mcp-server", version: "0.1.0" });

  server.registerTool(
    "stm32_get_project_snapshot",
    {
      title: "Get STM32 Project Snapshot",
      description: "Return the configured project root fingerprint, Git branch/HEAD/status, and tracked-file count. This is read-only and never contacts a remote.",
      inputSchema: z.object({}).strict(),
      outputSchema: z.object({
        project_root_sha256: z.string(), branch: z.string(), head: z.string(), status: z.string(), tracked_file_count: z.number()
      }),
      annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true, openWorldHint: false }
    },
    async () => {
      try {
        const [branch, head, status, tracked] = await Promise.all([
          git(["branch", "--show-current"]),
          git(["rev-parse", "HEAD"]),
          git(["status", "--short"]),
          git(["ls-files", "-z"])
        ]);
        for (const result of [branch, head, status, tracked]) {
          if (result.exit_code !== 0) throw new Error(result.stderr || "Git inspection failed.");
        }
        return toolSuccess({
          project_root_sha256: createHash("sha256").update(projectRoot.toLowerCase()).digest("hex"),
          branch: branch.stdout,
          head: head.stdout,
          status: status.stdout,
          tracked_file_count: tracked.stdout === "" ? 0 : tracked.stdout.split("\u0000").filter(Boolean).length
        });
      } catch (error) {
        return toolError(error, "Verify MCUFORGE_PROJECT_ROOT points to a valid Git worktree.");
      }
    }
  );

  server.registerTool(
    "stm32_list_project_files",
    {
      title: "List Tracked STM32 Project Files",
      description: "List tracked project files with optional case-insensitive substring filtering and pagination. Generated and untracked files are excluded.",
      inputSchema: z.object({
        query: z.string().max(200).default("").describe("Optional path substring, for example 'Core/Src' or 'BMI088'."),
        offset: z.number().int().min(0).default(0),
        limit: z.number().int().min(1).max(100).default(50)
      }).strict(),
      annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true, openWorldHint: false }
    },
    async ({ query, offset, limit }) => {
      try {
        const result = await git(["ls-files", "-z"]);
        if (result.exit_code !== 0) throw new Error(result.stderr || "Unable to list tracked files.");
        const needle = query.toLowerCase();
        const files = result.stdout.split("\u0000").filter(Boolean).filter(file => file.toLowerCase().includes(needle));
        const items = files.slice(offset, offset + limit);
        return toolSuccess({
          total_count: files.length,
          count: items.length,
          offset,
          files: items,
          has_more: offset + items.length < files.length,
          next_offset: offset + items.length < files.length ? offset + items.length : null
        });
      } catch (error) {
        return toolError(error, "Check that Git is installed and the configured worktree is readable.");
      }
    }
  );

  server.registerTool(
    "stm32_read_project_file",
    {
      title: "Read a Scoped STM32 Project File",
      description: "Read a UTF-8 text file inside the configured project root by relative path. Blocks path traversal, Git internals, dependency trees, likely secret files, binary extensions, and files over 256 KiB.",
      inputSchema: z.object({
        path: z.string().min(1).max(500).describe("Project-relative path, for example 'Core/Src/main.c'."),
        start_line: z.number().int().min(1).default(1),
        max_lines: z.number().int().min(1).max(500).default(200)
      }).strict(),
      annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true, openWorldHint: false }
    },
    async ({ path: requestedPath, start_line, max_lines }) => {
      try {
        const resolved = resolveProjectPath(requestedPath);
        const extension = path.extname(resolved).toLowerCase();
        if (!TEXT_EXTENSIONS.has(extension)) throw new Error(`File extension '${extension}' is not on the text allowlist.`);
        const stat = await fs.stat(resolved);
        if (!stat.isFile()) throw new Error("Requested path is not a regular file.");
        if (stat.size > FILE_SIZE_LIMIT) throw new Error(`File exceeds the ${FILE_SIZE_LIMIT}-byte read limit.`);
        const content = await fs.readFile(resolved, "utf8");
        const lines = content.split(/\r?\n/);
        const selected = lines.slice(start_line - 1, start_line - 1 + max_lines);
        return toolSuccess({
          path: path.relative(projectRoot, resolved).replaceAll("\\", "/"),
          sha256: createHash("sha256").update(content).digest("hex"),
          total_lines: lines.length,
          start_line,
          end_line: start_line + selected.length - 1,
          content: selected.join("\n"),
          truncated: start_line - 1 + selected.length < lines.length
        });
      } catch (error) {
        return toolError(error, "Use stm32_list_project_files to choose a tracked, non-secret text file inside the project.");
      }
    }
  );

  server.registerTool(
    "stm32_get_git_diff",
    {
      title: "Get Scoped STM32 Git Diff",
      description: "Return the current Git diff for the whole project or one validated project-relative path. Set cached=true to inspect the staged index. This never stages, resets, commits, fetches or contacts a remote.",
      inputSchema: z.object({
        path: z.string().max(500).optional().describe("Optional project-relative path to restrict the diff."),
        cached: z.boolean().default(false).describe("Read the staged index diff when true; read the worktree diff when false.")
      }).strict(),
      annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true, openWorldHint: false }
    },
    async ({ path: requestedPath, cached }) => {
      try {
        const args = cached ? ["diff", "--cached", "--no-ext-diff", "--"] : ["diff", "--no-ext-diff", "--"];
        if (requestedPath) {
          const resolved = resolveProjectPath(requestedPath);
          args.push(path.relative(projectRoot, resolved));
        }
        const result = await git(args);
        if (result.exit_code !== 0) throw new Error(result.stderr || "Unable to read Git diff.");
        return toolSuccess({ diff: result.stdout, cached, truncated: result.truncated });
      } catch (error) {
        return toolError(error, "Use a project-relative path returned by stm32_list_project_files.");
      }
    }
  );

  server.registerTool(
    "stm32_verify_test_integrity",
    {
      title: "Verify Immutable STM32 Testcases",
      description: "Run only the repository's fixed testcase hash audit. It reads the lock and testcase JSON files and returns the original machine-readable result; it does not run hardware tests or change files.",
      inputSchema: z.object({}).strict(),
      annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true, openWorldHint: false }
    },
    async () => {
      try {
        const script = resolveAgentPath("skills/stm32-evidence-audit/scripts/Test-TestcaseIntegrity.ps1");
        const result = await runCommand("pwsh.exe", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script, "-ProjectRoot", projectRoot, "-ProfileRoot", profileRoot], 30_000);
        if (result.exit_code !== 0) {
          return toolError(result.stdout || result.stderr || "Test integrity check failed.", "Treat the mismatch as a hard stop; do not edit the tests or lock to make it pass.");
        }
        return toolSuccess({ command_result: result, audit: JSON.parse(result.stdout) as unknown });
      } catch (error) {
        return toolError(error, "Verify PowerShell is available and the testcase lock remains intact.");
      }
    }
  );

  server.registerTool(
    "stm32_run_keil_build",
    {
      title: "Run Controlled STM32 Keil Build",
      description: "Run the repository's allowlisted Keil build wrapper and return its exact JSON evidence. This may update regenerable build outputs, but cannot flash hardware, open a serial port, run arbitrary commands, push, or modify source files.",
      inputSchema: z.object({
        rebuild: z.boolean().default(true).describe("Use a full Keil rebuild when true; otherwise use an incremental build.")
      }).strict(),
      annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: true, openWorldHint: false }
    },
    async ({ rebuild }) => {
      try {
        const script = resolveAgentPath("skills/stm32-keil-build/scripts/Invoke-KeilBuild.ps1");
        const args = ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script, "-ProjectRoot", projectRoot];
        if (rebuild) args.push("-Rebuild");
        const result = await runCommand("pwsh.exe", args, 5 * 60_000);
        let evidence: unknown = null;
        try { evidence = JSON.parse(result.stdout); } catch { evidence = null; }
        const output = { command_result: result, evidence };
        if (result.exit_code !== 0) {
          return toolError(JSON.stringify(output), "Read the Keil error log and return the failure evidence to the Firmware Agent; do not claim a successful build.");
        }
        return toolSuccess(output);
      } catch (error) {
        return toolError(error, "Verify Keil uVision and the VCW project are installed at the allowlisted locations.");
      }
    }
  );

  server.registerTool(
    "stm32_create_patch_proposal",
    {
      title: "Create Audited STM32 Patch Proposal",
      description: "Validate an Agent-produced unified diff against the frozen patch policy and record it under the host profile. This never writes project source, stages files, commits, pushes, flashes, or opens a COM port.",
      inputSchema: z.object({
        proposal_id: z.string().regex(/^[A-Za-z0-9][A-Za-z0-9._-]{2,63}$/).describe("Stable proposal identifier; letters, digits, dot, underscore and hyphen only."),
        patch: z.string().min(1).max(64 * 1024).describe("UTF-8 unified diff text produced by Firmware Agent.")
      }).strict(),
      annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: false, openWorldHint: false }
    },
    async ({ proposal_id, patch }) => {
      try {
        const script = resolveAgentPath("patch_channel/New-MCUForgePatchProposal.ps1");
        const policy = path.join(profileRoot, "patch-policy.json");
        const result = await withTemporaryPatch(patch, async patchFile => await runCommand("pwsh.exe", [
          "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
          "-PatchFile", patchFile,
          "-ProposalId", proposal_id,
          "-ProjectRoot", projectRoot,
          "-ProfileRoot", profileRoot,
          "-PolicyFile", policy
        ], 30_000));
        if (result.exit_code !== 0) {
          return toolError(result.stdout || result.stderr || "Patch proposal validation failed.", "Keep the Windows worktree unchanged, freeze a current patch policy if the baseline is stale, then submit a new proposal.");
        }
        const proposal = parseJsonResult(result.stdout);
        return toolSuccess({
          operation: "create_patch_proposal",
          proposal_id,
          status: proposal.status ?? "pending_human_review",
          proposal_directory: path.relative(profileRoot, resolveProposalDirectory(proposal_id)).replaceAll("\\", "/"),
          patch_sha256: proposal.patch_sha256 ?? null,
          changed_paths: proposal.changed_paths ?? [],
          next_action: "A human must inspect proposal.patch and proposal.json on Windows before supplying the exact approval token to stm32_apply_approved_patch."
        });
      } catch (error) {
        return toolError(error, "Verify the frozen patch policy and submit a valid unified diff without changing the project worktree.");
      }
    }
  );

  server.registerTool(
    "stm32_apply_approved_patch",
    {
      title: "Apply Reviewed STM32 Patch",
      description: "Materialize one previously recorded patch proposal into the configured worktree and Git index with an exact human APPLY token, or with an AUTO token when the Windows bridge was explicitly started in Autonomous Local Mode. Revalidates policy, Git HEAD, source hashes and patch hash; rejects skipped or empty applications; never commits, pushes, flashes, opens a COM port, or accepts an arbitrary path.",
      inputSchema: z.object({
        proposal_id: z.string().regex(/^[A-Za-z0-9][A-Za-z0-9._-]{2,63}$/),
        approval_token: z.string().regex(/^(?:APPLY|AUTO) [A-Za-z0-9][A-Za-z0-9._-]{2,63} [a-f0-9]{64}$/i).describe("Use APPLY <proposal_id> <sha256> after human review, or AUTO <proposal_id> <sha256> only when Autonomous Local Mode was explicitly enabled before the run.")
      }).strict(),
      annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: false, openWorldHint: false }
    },
    async ({ proposal_id, approval_token }) => {
      try {
        const isAutonomousApproval = /^AUTO /i.test(approval_token);
        if (isAutonomousApproval && !autonomousLocalMode) {
          return toolError(
            "AUTO approval was requested but the Windows bridge is not running in Autonomous Local Mode.",
            "Restart Start-STM32ToolBridge.ps1 with -EnableAutonomousLocalMode, or use the normal human APPLY approval token."
          );
        }
        const effectiveApprovalToken = isAutonomousApproval
          ? approval_token.replace(/^AUTO /i, "APPLY ")
          : approval_token;
        const proposalDirectory = resolveProposalDirectory(proposal_id);
        const script = resolveAgentPath("patch_channel/Apply-MCUForgeApprovedPatch.ps1");
        const policy = path.join(profileRoot, "patch-policy.json");
        const result = await runCommand("pwsh.exe", [
          "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
          "-ProposalDirectory", proposalDirectory,
          "-ApprovalToken", effectiveApprovalToken,
          "-ProjectRoot", projectRoot,
          "-ProfileRoot", profileRoot,
          "-PolicyFile", policy
        ], 30_000);
        if (result.exit_code !== 0) {
          return toolError(result.stdout || result.stderr || "Approved patch was not applied.", "No project change was authorized. Inspect the proposal, approval token, Git status and policy error, then retry only after the human resolves it.");
        }
        const applied = parseJsonResult(result.stdout);
        return toolSuccess({
          operation: "apply_approved_patch",
          proposal_id,
          approval_mode: isAutonomousApproval ? "autonomous_local" : "human",
          status: applied.status ?? "applied_to_git_index",
          proposal_directory: path.relative(profileRoot, proposalDirectory).replaceAll("\\", "/"),
          patch_sha256: applied.patch_sha256 ?? null,
          changed_paths: applied.changed_paths ?? [],
          git_head_before_apply: applied.git_head_before_apply ?? null,
          next_required_actions: applied.next_required_actions ?? [
            "Inspect git diff --cached, run fixed-test integrity and a real Keil build, then create a reviewed normal Git commit."
          ]
        });
      } catch (error) {
        return toolError(error, "Do not bypass the approval gate; inspect the proposal and retry with the exact human token.");
      }
    }
  );

  return server;
}

async function tokenMatches(authorization: string | undefined, bridgeHeader: string | undefined): Promise<boolean> {
  const candidate = bridgeHeader ?? (authorization?.startsWith("Bearer ") ? authorization.slice("Bearer ".length) : undefined);
  if (!candidate) return false;
  const supplied = Buffer.from(candidate);
  const expected = Buffer.from(bridgeToken);
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
  if (!Number.isInteger(port) || port < 1024 || port > 65_535) throw new Error("MCUFORGE_BRIDGE_PORT must be between 1024 and 65535.");
  if (bridgeToken.length < 32) throw new Error("MCUFORGE_BRIDGE_TOKEN must contain at least 32 characters.");
  for (const [name, configuredPath] of [["MCUFORGE_PROJECT_ROOT", projectRoot], ["MCUFORGE_AGENT_ROOT", agentRoot], ["MCUFORGE_PROFILE_ROOT", profileRoot]] as const) {
    const stat = await fs.stat(configuredPath);
    if (!stat.isDirectory()) throw new Error(`${name} is not a directory.`);
  }

  const app = createMcpExpressApp({ host: "0.0.0.0" });
  app.use(hostHeaderValidation(["localhost", "127.0.0.1", "host.docker.internal", "aigw-local.hiclaw.io"]));
  app.get("/health", (_req: Request, res: Response) => {
    res.json({ status: "ok", service: "stm32-tool-bridge-mcp-server" });
  });
  app.use(MCP_PATHS, async (req: Request, res: Response, next) => {
    if (!await tokenMatches(req.header("authorization"), req.header("x-mcuforge-bridge-token"))) {
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
    console.error(`STM32 Tool Bridge listening on port ${port}; project fingerprint ${createHash("sha256").update(projectRoot.toLowerCase()).digest("hex")}`);
  });
}

main().catch(error => {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
});
