// Ghidra pre-script for tools/ghidra/setup.ps1: tune auto-analysis for a large UE3 exe.
// Part of ME VR Attempt (GPL-3.0). Runs headless before the import's auto-analysis.
//
// The exe is ~16 MB of code with ~200k functions in .pdata. The defaults are fine except the
// slow, optional decompiler-driven passes, which would multiply the analysis time; we decompile
// on demand instead (tools/ghidra/decompile.ps1).
// @category MEVR

import java.util.Map;

import ghidra.app.script.GhidraScript;

public class MevrPreAnalysis extends GhidraScript {
	@Override
	protected void run() throws Exception {
		Map<String, String> current = getCurrentAnalysisOptionsAndValues(currentProgram);
		String[][] off = {
			{ "Decompiler Parameter ID", "false" },
			{ "Aggressive Instruction Finder", "false" },
		};
		for (String[] o : off) {
			if (current.containsKey(o[0])) {
				setAnalysisOption(currentProgram, o[0], o[1]);
				println("MEVR: analysis option '" + o[0] + "' = " + o[1] + " (was " + current.get(o[0]) + ")");
			}
			else {
				println("MEVR: no analysis option '" + o[0] + "' in this Ghidra version");
			}
		}
	}
}
