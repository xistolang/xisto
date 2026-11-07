// The VS Code side of the Xisto debugger: VS Code runs `xisto dap` (a debug adapter over stdin/stdout) and speaks the
// Debug Adapter Protocol with it. Everything else (building, the agent inside the program) is xisto's.
const vscode = require("vscode");

function activate(context) {
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory("xisto", {
      createDebugAdapterDescriptor() {
        const xisto = vscode.workspace.getConfiguration("xisto").get("path") || "xisto";
        return new vscode.DebugAdapterExecutable(xisto, ["dap"]);
      },
    }),
    vscode.debug.registerDebugConfigurationProvider("xisto", {
      // F5 with no launch.json: debug the file in the editor
      resolveDebugConfiguration(folder, config) {
        if (!config.type && !config.request && !config.name) {
          const editor = vscode.window.activeTextEditor;
          if (editor && editor.document.languageId === "typescript") {
            return { type: "xisto", request: "launch", name: "Xisto: debug this file", program: editor.document.fileName, cwd: folder ? folder.uri.fsPath : undefined };
          }
        }
        if (config.request === "attach") return config;
        if (!config.program) {
          return vscode.window.showErrorMessage("Xisto: set \"program\" to the .ts file to debug").then(() => undefined);
        }
        return config;
      },
    })
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
