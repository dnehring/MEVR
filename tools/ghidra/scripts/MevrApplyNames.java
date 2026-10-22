// Applies names from CSV files (rva,name,kind,comment) to the current program.
// Part of ME VR Attempt (GPL-3.0). Run by tools/ghidra/seed.ps1 as a headless postScript.
//
// Arguments: one or more CSV paths, applied in order; a later file wins a clash at one address
// (the earlier name stays as a secondary label). Rows:
//   rva   hex RVA (image base is added)
//   name  "A::B::Leaf" puts Leaf in namespace A::B
//   kind  func (create a function there if there is none) | label | data
//   comment  optional; becomes the function's plate comment (or the label's EOL comment)
// Idempotent: rerunning with the same files changes nothing.
// @category MEVR

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

public class MevrApplyNames extends GhidraScript {
	private int renamed, created, labels, unchanged, failed;

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length == 0) {
			printerr("usage: MevrApplyNames.java <csv> [<csv> ...]");
			return;
		}
		for (String path : args) {
			int before = renamed + created + labels;
			int rows = applyFile(new File(path));
			println(String.format("MEVR: %s: %d rows, %d names applied", path, rows,
				renamed + created + labels - before));
		}
		println(String.format(
			"MEVR: seeding done: %d functions renamed, %d functions created, %d labels, %d unchanged, %d failed",
			renamed, created, labels, unchanged, failed));
	}

	private int applyFile(File file) throws Exception {
		int rows = 0;
		try (BufferedReader in = new BufferedReader(new FileReader(file))) {
			String line;
			while ((line = in.readLine()) != null) {
				monitor.checkCancelled();
				line = line.trim();
				if (line.isEmpty() || line.startsWith("#") || line.startsWith("rva,")) {
					continue;
				}
				String[] p = line.split(",", 4);
				if (p.length < 3) {
					printerr("MEVR: bad row: " + line);
					failed++;
					continue;
				}
				rows++;
				try {
					long rva = Long.decode(p[0].trim());
					apply(rva, p[1].trim(), p[2].trim(), p.length > 3 ? p[3].trim() : "");
				}
				catch (Exception e) {
					printerr("MEVR: " + line + ": " + e.getMessage());
					failed++;
				}
			}
		}
		return rows;
	}

	private void apply(long rva, String fullName, String kind, String comment) throws Exception {
		Address addr = currentProgram.getImageBase().add(rva);
		MemoryBlock block = currentProgram.getMemory().getBlock(addr);
		if (block == null) {
			throw new IllegalArgumentException("not in the image");
		}
		int split = fullName.lastIndexOf("::");
		String leaf = split < 0 ? fullName : fullName.substring(split + 2);
		Namespace ns = split < 0 ? currentProgram.getGlobalNamespace()
				: NamespaceUtils.createNamespaceHierarchy(fullName.substring(0, split), null,
					currentProgram, SourceType.USER_DEFINED);

		Function f = getFunctionAt(addr);
		if (f == null && kind.equals("func") && block.isExecute()) {
			if (getInstructionAt(addr) == null) {
				new DisassembleCommand(addr, null, true).applyTo(currentProgram, monitor);
			}
			CreateFunctionCmd cmd = new CreateFunctionCmd(addr);
			if (cmd.applyTo(currentProgram, monitor)) {
				f = getFunctionAt(addr);
				created++;
			}
		}
		if (f != null) {
			if (f.getName().equals(leaf) && f.getParentNamespace().equals(ns)) {
				unchanged++;
			}
			else {
				if (f.getSymbol().getSource() == SourceType.USER_DEFINED) {
					// Keep the earlier name as a label (e.g. the natives dump's exec name).
					addLabel(addr, f.getName(), f.getParentNamespace());
				}
				// A label of the target name here (from an earlier run) would clash.
				Symbol same = currentProgram.getSymbolTable().getSymbol(leaf, addr, ns);
				if (same != null && !same.equals(f.getSymbol())) {
					same.delete();
				}
				f.getSymbol().setNameAndNamespace(leaf, ns, SourceType.USER_DEFINED);
				renamed++;
			}
			if (!comment.isEmpty()) {
				String plate = f.getComment();
				if (plate == null || !plate.contains(comment)) {
					f.setComment(plate == null ? comment : plate + "\n" + comment);
				}
			}
			return;
		}
		Symbol s = addLabel(addr, leaf, ns);
		if (s != null && !s.isPrimary()) {
			s.setPrimary();
		}
		if (!comment.isEmpty()) {
			CodeUnit cu = currentProgram.getListing().getCodeUnitAt(addr);
			if (cu != null && (cu.getComment(CodeUnit.EOL_COMMENT) == null ||
				!cu.getComment(CodeUnit.EOL_COMMENT).contains(comment))) {
				cu.setComment(CodeUnit.EOL_COMMENT, comment);
			}
		}
	}

	private Symbol addLabel(Address addr, String name, Namespace ns) throws Exception {
		SymbolTable st = currentProgram.getSymbolTable();
		Symbol existing = st.getSymbol(name, addr, ns);
		if (existing != null) {
			unchanged++;
			return existing;
		}
		labels++;
		return st.createLabel(addr, name, ns, SourceType.USER_DEFINED);
	}
}
