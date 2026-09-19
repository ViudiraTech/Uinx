import * as cp from 'child_process';
import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;
let output: vscode.OutputChannel;
let fallbackDiagnostics: vscode.DiagnosticCollection;
let statusBar: vscode.StatusBarItem;

function cfg<T>(key: string, fallback: T): T {
  return vscode.workspace.getConfiguration('uinx').get<T>(key, fallback);
}

function resolveCommand(configured: string, fallbackNames: string[]): string {
  if (configured && configured.trim().length > 0) {
    return configured;
  }
  for (const name of fallbackNames) {
    try {
      cp.execFileSync(name, ['--help'], { stdio: 'ignore', timeout: 3000 });
      return name;
    } catch {
      // try next
    }
  }
  return fallbackNames[0];
}

function workspaceRoot(): vscode.Uri | undefined {
  const editor = vscode.window.activeTextEditor;
  if (editor) {
    const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
    if (folder) {
      return folder.uri;
    }
  }
  return vscode.workspace.workspaceFolders?.[0]?.uri;
}

function extraCompilerArgs(): string[] {
  const args: string[] = [];
  const smp = cfg<string>('smp', 'auto');
  if (smp) {
    args.push(`--smp=${smp}`);
  }
  const target = cfg<string>('target', '');
  if (target && target.trim().length > 0) {
    args.push(`--target=${target.trim()}`);
  }
  if (cfg<boolean>('enableHeader', false)) {
    args.push('-enable-header');
    for (const dir of cfg<string[]>('includeDirs', [])) {
      args.push('-I', dir);
    }
  }
  return args;
}

async function startLanguageServer(context: vscode.ExtensionContext): Promise<void> {
  await stopLanguageServer();
  const serverCmd = resolveCommand(cfg<string>('server.path', 'uinx-lsp'), ['uinx-lsp']);
  const serverOptions: ServerOptions = {
    command: serverCmd,
    args: [],
    transport: TransportKind.stdio,
  };
  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: 'file', language: 'uinx' }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher('**/*.{ux,uxh}'),
    },
    traceOutputChannel: output,
  };
  client = new LanguageClient('uinx-lsp', 'Uinx Language Server', serverOptions, clientOptions);
  const trace = cfg<string>('trace.server', 'off');
  if (trace !== 'off') {
    await client.setTrace(trace === 'verbose' ? 2 : 1);
  }
  try {
    await client.start();
    statusBar.text = '$(check) Uinx LSP';
    statusBar.tooltip = `uinx-lsp running (${serverCmd})`;
  } catch (err) {
    output.appendLine(`[uinx] language server failed to start (${serverCmd}): ${err}`);
    statusBar.text = '$(warning) Uinx (uinxc fallback)';
    statusBar.tooltip = 'uinx-lsp unavailable, using uinxc diagnostics on save';
    void vscode.window.showWarningMessage(
      'Uinx language server not found. Falling back to uinxc diagnostics. Set uinx.server.path.'
    );
  }
}

async function stopLanguageServer(): Promise<void> {
  if (client) {
    try {
      await client.stop();
    } catch {
      // ignore
    }
    client = undefined;
  }
}

function parseUinxcDiagnostics(stderr: string): vscode.Diagnostic[] {
  // file:line:col: error[EXXXX]: message
  const out: vscode.Diagnostic[] = [];
  const re = /^(.*?):(\d+):(\d+):\s+(error|warning)(?:\[([A-Z0-9]+)\])?:\s*(.*)$/gm;
  let m: RegExpExecArray | null;
  while ((m = re.exec(stderr)) !== null) {
    const line = Math.max(0, parseInt(m[2], 10) - 1);
    const col = Math.max(0, parseInt(m[3], 10) - 1);
    const severity =
      m[4] === 'error' ? vscode.DiagnosticSeverity.Error : vscode.DiagnosticSeverity.Warning;
    const range = new vscode.Range(line, col, line, col + 1);
    const code = m[5] ? `${m[4]}[${m[5]}]` : m[4];
    const message = `${m[6]} (${code})`;
    const d = new vscode.Diagnostic(range, message, severity);
    d.source = 'uinx';
    if (m[5]) {
      d.code = m[5];
    }
    out.push(d);
  }
  return out;
}

async function refreshFallbackDiagnostics(doc: vscode.TextDocument): Promise<void> {
  if (doc.languageId !== 'uinx') {
    return;
  }
  if (client?.isRunning()) {
    return;
  }
  if (!cfg<boolean>('diagnostics.fallbackToUinxc', true)) {
    return;
  }
  const uinxc = resolveCommand(cfg<string>('uinxc.path', 'uinxc'), ['uinxc']);
  const args = [doc.fileName, '--emit=check', ...extraCompilerArgs()];
  cp.execFile(uinxc, args, { timeout: 15000 }, (_err, _stdout, stderr) => {
    const diags = parseUinxcDiagnostics(stderr ?? '');
    fallbackDiagnostics.set(doc.uri, diags);
  });
}

function runInTerminal(title: string, cmd: string, cwd?: string): void {
  let term = vscode.window.terminals.find((t) => t.name === title);
  if (!term) {
    term = vscode.window.createTerminal({ name: title, cwd });
  }
  term.show(true);
  term.sendText(cmd);
}

function quoted(p: string): string {
  if (/[\s"]/.test(p)) {
    return `"${p.replace(/"/g, '\\"')}"`;
  }
  return p;
}

function registerCommands(context: vscode.ExtensionContext): void {
  const tool = () => resolveCommand(cfg<string>('tool.path', 'uinx'), ['uinx']);
  const uinxc = () => resolveCommand(cfg<string>('uinxc.path', 'uinxc'), ['uinxc']);

  context.subscriptions.push(
    vscode.commands.registerCommand('uinx.check', async () => {
      const root = workspaceRoot();
      const smp = cfg<string>('smp', 'auto');
      runInTerminal('Uinx', `${quoted(tool())} check --smp=${smp}`, root?.fsPath);
    }),
    vscode.commands.registerCommand('uinx.build', async () => {
      const root = workspaceRoot();
      const smp = cfg<string>('smp', 'auto');
      runInTerminal('Uinx', `${quoted(tool())} build --smp=${smp}`, root?.fsPath);
    }),
    vscode.commands.registerCommand('uinx.run', async () => {
      const root = workspaceRoot();
      runInTerminal('Uinx', `${quoted(tool())} run`, root?.fsPath);
    }),
    vscode.commands.registerCommand('uinx.test', async () => {
      const root = workspaceRoot();
      runInTerminal('Uinx', `${quoted(tool())} test`, root?.fsPath);
    }),
    vscode.commands.registerCommand('uinx.fmt', async () => {
      const root = workspaceRoot();
      runInTerminal('Uinx', `${quoted(tool())} fmt`, root?.fsPath);
    }),
    vscode.commands.registerCommand('uinx.lint', async () => {
      const root = workspaceRoot();
      runInTerminal('Uinx', `${quoted(tool())} lint`, root?.fsPath);
    }),
    vscode.commands.registerCommand('uinx.newProject', async () => {
      const kind = await vscode.window.showQuickPick(
        [
          { label: 'app', description: 'Hosted application' },
          { label: 'lib', description: 'Library' },
          { label: 'freestanding', description: 'Freestanding binary' },
          { label: 'kernel:x86_64', description: 'Bare-metal kernel (x86_64)' },
          { label: 'kernel:aarch64', description: 'Bare-metal kernel (aarch64)' },
          { label: 'kernel:riscv64', description: 'Bare-metal kernel (riscv64)' },
        ],
        { placeHolder: 'Select Uinx project template' }
      );
      if (!kind) {
        return;
      }
      const name = await vscode.window.showInputBox({
        prompt: 'Project name',
        value: 'myuinx',
        validateInput: (v) => (v.trim().length === 0 ? 'Name is required' : undefined),
      });
      if (!name) {
        return;
      }
      const folders = await vscode.window.showOpenDialog({
        canSelectFiles: false,
        canSelectFolders: true,
        canSelectMany: false,
        openLabel: 'Create project here',
      });
      if (!folders || folders.length === 0) {
        return;
      }
      let args = '';
      if (kind.label.startsWith('kernel:')) {
        const arch = kind.label.split(':')[1];
        args = `new ${quoted(name.trim())} --kernel=${arch}`;
      } else if (kind.label === 'lib') {
        args = `new ${quoted(name.trim())} --lib`;
      } else if (kind.label === 'freestanding') {
        args = `new ${quoted(name.trim())} --freestanding`;
      } else {
        args = `new ${quoted(name.trim())}`;
      }
      runInTerminal('Uinx', `${quoted(tool())} ${args}`, folders[0].fsPath);
    }),
    vscode.commands.registerCommand('uinx.restartServer', async () => {
      await startLanguageServer(context);
      vscode.window.showInformationMessage('Uinx language server restarted.');
    }),
    vscode.commands.registerCommand('uinx.showIR', async () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.languageId !== 'uinx') {
        void vscode.window.showWarningMessage('Open a .ux file first.');
        return;
      }
      const file = editor.document.fileName;
      const args = [file, '--emit=llvm-ir', ...extraCompilerArgs()];
      cp.execFile(uinxc(), args, { timeout: 20000 }, (err, stdout, stderr) => {
        if (err) {
          output.appendLine(`[uinx] showIR failed: ${stderr || err.message}`);
          void vscode.window.showErrorMessage('uinxc LLVM IR failed. See Uinx output.');
          return;
        }
        const doc = vscode.window.createWebviewPanel(
          'uinxIR',
          `LLVM IR — ${path.basename(file)}`,
          vscode.ViewColumn.Beside,
          { enableFindWidget: true }
        );
        const escaped = stdout
          .replace(/&/g, '&amp;')
          .replace(/</g, '&lt;')
          .replace(/>/g, '&gt;');
        doc.webview.html = `<!DOCTYPE html><html><body style="background:#0F1B2D;color:#D7E3FF;font-family:monospace;white-space:pre;padding:16px">${escaped}</body></html>`;
      });
    })
  );

  // Fallback diagnostics on save/open when LSP is down.
  context.subscriptions.push(
    vscode.workspace.onDidSaveTextDocument((d) => void refreshFallbackDiagnostics(d)),
    vscode.workspace.onDidOpenTextDocument((d) => void refreshFallbackDiagnostics(d)),
    vscode.workspace.onDidChangeConfiguration(async (e) => {
      if (e.affectsConfiguration('uinx')) {
        await startLanguageServer(context);
      }
    })
  );

  // Optional format-on-save via `uinx fmt <file>` (indentation canonicalizer).
  context.subscriptions.push(
    vscode.workspace.onWillSaveTextDocument((e) => {
      if (e.document.languageId !== 'uinx') {
        return;
      }
      if (!cfg<boolean>('format.onSave', false)) {
        return;
      }
      // Best-effort synchronous format; failures surface in output.
      try {
        cp.execFileSync(tool(), ['fmt', e.document.fileName], { timeout: 10000 });
        // Reload is handled by VS Code watching the file; no edit needed here.
      } catch (err) {
        output.appendLine(`[uinx] fmt on save failed: ${err}`);
      }
    })
  );
}

class UinxTaskProvider implements vscode.TaskProvider {
  provideTasks(): vscode.Task[] {
    const tasks: vscode.Task[] = [];
    for (const cmd of ['check', 'build', 'run', 'test', 'fmt', 'lint'] as const) {
      const def: vscode.TaskDefinition & { command: string } = { type: 'uinx', command: cmd };
      const task = new vscode.Task(
        def,
        vscode.TaskScope.Workspace,
        `uinx ${cmd}`,
        'uinx',
        new vscode.ShellExecution(`${resolveCommand(cfg<string>('tool.path', 'uinx'), ['uinx'])} ${cmd}`),
        ['$uinx']
      );
      task.group =
        cmd === 'build' ? vscode.TaskGroup.Build : cmd === 'test' ? vscode.TaskGroup.Test : undefined;
      tasks.push(task);
    }
    return tasks;
  }

  resolveTask(task: vscode.Task): vscode.Task | undefined {
    const def = task.definition as { command?: string; smp?: string };
    if (!def.command) {
      return undefined;
    }
    const smp = def.smp ? ` --smp=${def.smp}` : '';
    task.execution = new vscode.ShellExecution(
      `${resolveCommand(cfg<string>('tool.path', 'uinx'), ['uinx'])} ${def.command}${smp}`
    );
    return task;
  }
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  output = vscode.window.createOutputChannel('Uinx');
  fallbackDiagnostics = vscode.languages.createDiagnosticCollection('uinx-uinxc');
  statusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
  statusBar.text = '$(sync~spin) Uinx';
  statusBar.tooltip = 'Uinx toolchain';
  statusBar.command = 'uinx.check';
  statusBar.show();

  context.subscriptions.push(output, fallbackDiagnostics, statusBar);
  registerCommands(context);
  context.subscriptions.push(
    vscode.tasks.registerTaskProvider('uinx', new UinxTaskProvider()),
    vscode.languages.registerDocumentFormattingEditProvider('uinx', {
      provideDocumentFormattingEdits(doc) {
        // Delegate to `uinx fmt` semantics is file-based; report no in-memory
        // edits here to avoid double-formatting. The uinx.fmt command and
        // format.onSave cover formatting.
        void doc;
        return [];
      },
    })
  );

  // Warn once if neither binary is on PATH.
  try {
    cp.execFileSync(resolveCommand(cfg<string>('uinxc.path', 'uinxc'), ['uinxc']), ['--help'], {
      stdio: 'ignore',
      timeout: 3000,
    });
  } catch {
    void vscode.window.showWarningMessage(
      'uinxc not found on PATH. Install the Uinx toolchain or set uinx.uinxc.path.'
    );
  }

  await startLanguageServer(context);
  output.appendLine('[uinx] extension activated.');
}

export async function deactivate(): Promise<void> {
  await stopLanguageServer();
}

export function __testOnly(): unknown {
  return { parseUinxcDiagnostics };
}
