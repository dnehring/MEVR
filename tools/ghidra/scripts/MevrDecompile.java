// Decompile / search / rename / comment, for tools/ghidra/decompile.ps1 (headless postScript).
// Part of ME VR Attempt (GPL-3.0).
//
// Arguments: <out file> <cache dir> <mode> <args...>, or one "@<file>" holding them one per line
// (UTF-8): analyzeHeadless.bat mangles arguments with cmd's special characters, e.g. parentheses.
//   decompile <item>...       item: 0xB72D30 | exe+0xB72D30 | 0x140B72D30 | B72D30 | a name
//   search <substring>        functions and our labels whose qualified name contains it
//   rename <item> <A::B::name>
//   comment <item> <text>     appends to the function's plate comment
// Names: an exact qualified name ("ABioWeaponRanged::GetMuzzleLoc"), its last part
// ("GetMuzzleLoc"), or else a unique case-insensitive substring.
// The report goes to <out file>; each decompiled function also to <cache dir>/0x<RVA>.c.
// Lines starting "MEVR-RESULT " are for the calling script.
// @category MEVR

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.TreeMap;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class MevrDecompile extends GhidraScript {
	private PrintWriter out;
	private File cacheDir;
	private long base;
	private DecompInterface decompiler;

	@Override
	protected void run() throws Exception {
		String[] a = getScriptArgs();
		if (a.length == 1 && a[0].startsWith("@")) {
			a = java.nio.file.Files.readAllLines(java.nio.file.Path.of(a[0].substring(1)),
				java.nio.charset.StandardCharsets.UTF_8).toArray(new String[0]);
		}
		if (a.length < 3) {
			printerr("usage: MevrDecompile.java <out> <cacheDir> <mode> <args...>");
			return;
		}
		base = currentProgram.getImageBase().getOffset();
		cacheDir = new File(a[1]);
		cacheDir.mkdirs();
		try (PrintWriter w = new PrintWriter(new FileWriter(a[0]))) {
			out = w;
			String mode = a[2];
			switch (mode) {
				case "decompile":
					for (int i = 3; i < a.length; i++) {
						decompile(a[i]);
					}
					break;
				case "search":
					search(a.length > 3 ? a[3] : "");
					break;
				case "rename":
					rename(a[3], a[4]);
					break;
				case "comment":
					comment(a[3], a[4]);
					break;
				default:
					out.println("unknown mode " + mode);
			}
		}
		finally {
			if (decompiler != null) {
				decompiler.dispose();
			}
		}
	}

	private String rva(Address addr) {
		return String.format("0x%X", addr.getOffset() - base);
	}

	private String describe(Function f) {
		return f.getName(true) + " " + rva(f.getEntryPoint());
	}

	// Parses the address forms; null for anything else (a name).
	private Address parseTarget(String item) {
		String t = item.trim();
		boolean prefixed = false;
		if (t.regionMatches(true, 0, "exe+", 0, 4)) {
			t = t.substring(4).trim();
			prefixed = true;
		}
		if (t.regionMatches(true, 0, "0x", 0, 2)) {
			t = t.substring(2);
			prefixed = true;
		}
		if (!t.matches("[0-9A-Fa-f]+") || (!prefixed && t.length() < 5)) {
			return null;
		}
		long v = Long.parseUnsignedLong(t, 16);
		if (v < base) {
			v += base;
		}
		return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
	}

	// Resolves an item to an address; prints candidates and returns null when ambiguous.
	private Address resolve(String item) {
		Address addr = parseTarget(item);
		if (addr != null) {
			return addr;
		}
		String q = item.trim();
		String leaf = q.contains("::") ? q.substring(q.lastIndexOf("::") + 2) : q;
		List<Symbol> exact = new ArrayList<>();
		SymbolIterator it = currentProgram.getSymbolTable().getSymbols(leaf);
		while (it.hasNext()) {
			Symbol s = it.next();
			String full = s.getName(true);
			if (full.equals(q) || full.endsWith("::" + q) || !q.contains("::")) {
				exact.add(s);
			}
		}
		if (exact.size() == 1) {
			return exact.get(0).getAddress();
		}
		List<String> hits = new ArrayList<>();
		Address only = null;
		if (exact.isEmpty()) {
			String lower = q.toLowerCase();
			FunctionIterator fi = currentProgram.getFunctionManager().getFunctions(true);
			while (fi.hasNext()) {
				Function f = fi.next();
				if (f.getName(true).toLowerCase().contains(lower)) {
					hits.add(describe(f));
					only = f.getEntryPoint();
				}
			}
		}
		else {
			for (Symbol s : exact) {
				hits.add(s.getName(true) + " " + rva(s.getAddress()));
			}
		}
		if (hits.size() == 1) {
			return only;
		}
		out.println("// " + q + ": " + (hits.isEmpty() ? "no match" : hits.size() + " matches, be more specific:"));
		for (int i = 0; i < Math.min(hits.size(), 50); i++) {
			out.println("//   " + hits.get(i));
		}
		if (hits.size() > 50) {
			out.println("//   ...");
		}
		return null;
	}

	// The function at or containing addr; creates one (unsaved under -readOnly) if there is none.
	private Function functionFor(Address addr) throws Exception {
		Function f = getFunctionContaining(addr);
		if (f != null) {
			return f;
		}
		MemoryBlock block = currentProgram.getMemory().getBlock(addr);
		if (block == null || !block.isExecute()) {
			return null;
		}
		if (getInstructionAt(addr) == null) {
			new DisassembleCommand(addr, null, true).applyTo(currentProgram, monitor);
		}
		new CreateFunctionCmd(addr).applyTo(currentProgram, monitor);
		f = getFunctionAt(addr);
		if (f != null) {
			out.println("// (no function was defined at " + rva(addr) + "; created one for this run)");
		}
		return f;
	}

	private void decompile(String item) throws Exception {
		Address addr = resolve(item);
		if (addr == null) {
			out.println();
			return;
		}
		Function f = functionFor(addr);
		if (f == null) {
			out.println("// " + item + " -> " + rva(addr) + ": not code (" + symbolAt(addr) + ")");
			out.println();
			return;
		}
		StringBuilder sb = new StringBuilder();
		Address entry = f.getEntryPoint();
		sb.append(String.format("// ==== %s  RVA %s  (exe+%s, 0x%X)  %d bytes\n", f.getName(true), rva(entry),
			rva(entry), entry.getOffset(), f.getBody().getNumAddresses()));
		if (!addr.equals(entry)) {
			sb.append(String.format("// %s is inside it, at +0x%X\n", rva(addr), addr.subtract(entry)));
		}
		if (hookPatched(entry)) {
			sb.append("// WARNING: starts with a jmp out of the image: a hook (ours, Halcyon's or another ASI's)\n" +
				"// had patched it when the .text was dumped, so its first bytes are lost and the body is cut\n" +
				"// short. Use a dump taken at attach (engine/CodeDump), then tools/ghidra/setup.ps1 -Force.\n");
		}

		TreeMap<Address, Function> callers = new TreeMap<>();
		for (Function c : f.getCallingFunctions(monitor)) {
			callers.put(c.getEntryPoint(), c);
		}
		List<String> dataRefs = pointersTo(entry);
		Set<Function> calleeSet = f.getCalledFunctions(monitor);
		TreeMap<Address, Function> callees = new TreeMap<>();
		for (Function c : calleeSet) {
			callees.put(c.getEntryPoint(), c);
		}
		sb.append("// callers (").append(callers.size()).append("):");
		appendList(sb, callers);
		if (!dataRefs.isEmpty()) {
			sb.append("// pointers to it in data (").append(dataRefs.size()).append("; vtable start guessed from the run of code pointers):");
			for (int i = 0; i < Math.min(dataRefs.size(), 20); i++) {
				sb.append(i % 3 == 0 ? "\n//   " : ", ").append(dataRefs.get(i));
			}
			sb.append(dataRefs.size() > 20 ? "\n//   ...\n" : "\n");
		}
		sb.append("// callees (").append(callees.size()).append("):");
		appendList(sb, callees);

		if (decompiler == null) {
			decompiler = new DecompInterface();
			DecompileOptions options = new DecompileOptions();
			options.grabFromProgram(currentProgram);
			decompiler.setOptions(options);
			decompiler.toggleCCode(true);
			decompiler.toggleSyntaxTree(false);
			decompiler.setSimplificationStyle("decompile");
			if (!decompiler.openProgram(currentProgram)) {
				throw new IllegalStateException("decompiler: " + decompiler.getLastMessage());
			}
		}
		DecompileResults res = decompiler.decompileFunction(f, 180, monitor);
		if (res.decompileCompleted() && res.getDecompiledFunction() != null) {
			sb.append(res.getDecompiledFunction().getC());
		}
		else {
			sb.append("// decompile failed: ").append(res.getErrorMessage()).append('\n');
		}
		String text = sb.toString();
		out.println(text);
		try (PrintWriter w = new PrintWriter(new FileWriter(new File(cacheDir, rva(entry) + ".c")))) {
			w.print(text);
		}
		out.println("MEVR-RESULT cached " + rva(entry) + (addr.equals(entry) ? "" : " " + rva(addr)));
		out.println();
	}

	// 8-byte pointers to entry in the data sections (vtables, native tables). For each, guesses
	// the vtable start: the first of the contiguous code pointers around it.
	private List<String> pointersTo(Address entry) throws Exception {
		List<String> found = new ArrayList<>();
		long target = entry.getOffset();
		byte[] needle = new byte[8];
		for (int i = 0; i < 8; i++) {
			needle[i] = (byte) (target >>> (8 * i));
		}
		ghidra.program.model.mem.Memory mem = currentProgram.getMemory();
		for (MemoryBlock b : mem.getBlocks()) {
			String n = b.getName();
			if (b.isExecute() || !b.isInitialized() || n.equals(".pdata") || n.equals(".reloc") || n.equals(".rsrc")) {
				continue;
			}
			Address at = b.getStart();
			while (at != null && at.compareTo(b.getEnd()) <= 0 && found.size() < 20) {
				Address hit = mem.findBytes(at, b.getEnd(), needle, null, true, monitor);
				if (hit == null) {
					break;
				}
				Address first = hit;
				for (int i = 0; i < 4096; i++) {
					Address prev = first.subtract(8);
					if (!isCodePointer(mem, prev)) {
						break;
					}
					first = prev;
				}
				String s = rva(hit) + " in " + n;
				if (isCodePointer(mem, hit.subtract(8)) || isCodePointer(mem, hit.add(8))) {
					s += String.format(" = vtable? %s + 0x%X", rva(first), hit.subtract(first));
				}
				found.add(s);
				at = hit.add(1);
			}
		}
		return found;
	}

	// A 5-byte jmp rel32 whose target lies outside the program (a hook's trampoline).
	private boolean hookPatched(Address entry) {
		try {
			ghidra.program.model.mem.Memory mem = currentProgram.getMemory();
			if (mem.getByte(entry) != (byte) 0xE9) {
				return false;
			}
			long target = entry.getOffset() + 5 + mem.getInt(entry.add(1));
			Address to = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(target);
			return mem.getBlock(to) == null;
		}
		catch (Exception e) {
			return true;  // the target doesn't even form an address
		}
	}

	private boolean isCodePointer(ghidra.program.model.mem.Memory mem, Address a) {
		try {
			long v = mem.getLong(a);
			Address to = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
			MemoryBlock b = mem.getBlock(to);
			return b != null && b.isExecute();
		}
		catch (Exception e) {
			return false;
		}
	}

	private void appendList(StringBuilder sb, TreeMap<Address, Function> fs) {
		int i = 0;
		for (Function c : fs.values()) {
			if (i == 60) {
				sb.append("\n//   ...");
				break;
			}
			sb.append(i % 3 == 0 ? "\n//   " : ", ").append(describe(c));
			i++;
		}
		sb.append('\n');
	}

	private String symbolAt(Address addr) {
		Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(addr);
		MemoryBlock b = currentProgram.getMemory().getBlock(addr);
		return (s != null ? s.getName(true) : "no symbol") + (b != null ? " in " + b.getName() : "");
	}

	private void search(String text) {
		String lower = text.toLowerCase();
		int n = 0;
		FunctionIterator fi = currentProgram.getFunctionManager().getFunctions(true);
		while (fi.hasNext()) {
			Function f = fi.next();
			if (f.getName(true).toLowerCase().contains(lower)) {
				if (n++ < 500) {
					out.println(String.format("%-10s %s  (%d bytes)", rva(f.getEntryPoint()), f.getName(true),
						f.getBody().getNumAddresses()));
				}
			}
		}
		SymbolIterator si = currentProgram.getSymbolTable().getAllSymbols(true);
		while (si.hasNext()) {
			Symbol s = si.next();
			if (s.getSource() == SourceType.USER_DEFINED && getFunctionAt(s.getAddress()) == null &&
				s.getName(true).toLowerCase().contains(lower)) {
				if (n++ < 500) {
					out.println(String.format("%-10s %s  (label)", rva(s.getAddress()), s.getName(true)));
				}
			}
		}
		out.println(String.format("// %d matches for '%s'%s", n, text, n > 500 ? " (first 500 shown)" : ""));
	}

	private void rename(String item, String newName) throws Exception {
		Address addr = resolve(item);
		Function f = addr == null ? null : functionFor(addr);
		if (f == null) {
			out.println("// rename: no function for " + item);
			return;
		}
		int split = newName.lastIndexOf("::");
		Namespace ns = split < 0 ? currentProgram.getGlobalNamespace()
				: NamespaceUtils.createNamespaceHierarchy(newName.substring(0, split), null, currentProgram,
					SourceType.USER_DEFINED);
		String old = f.getName(true);
		f.getSymbol().setNameAndNamespace(split < 0 ? newName : newName.substring(split + 2), ns,
			SourceType.USER_DEFINED);
		out.println("// renamed " + old + " -> " + f.getName(true) + " at " + rva(f.getEntryPoint()));
		out.println("MEVR-RESULT renamed " + rva(f.getEntryPoint()) + " " + f.getName(true));
	}

	private void comment(String item, String text) throws Exception {
		Address addr = resolve(item);
		Function f = addr == null ? null : functionFor(addr);
		if (f == null) {
			out.println("// comment: no function for " + item);
			return;
		}
		String plate = f.getComment();
		f.setComment(plate == null || plate.isEmpty() ? text : plate + "\n" + text);
		out.println("// commented " + describe(f) + ": " + text);
		out.println("MEVR-RESULT commented " + rva(f.getEntryPoint()) + " " + f.getName(true));
	}
}
