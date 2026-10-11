// SPDX-License-Identifier: GPL-3.0-or-later
// Ghidra headless post-script: export the control-flow facts the recompiler
// needs from a Firebeat program image, as JSON.
//
//   analyzeHeadless <proj> <name> -import program.bin -processor PowerPC:BE:32:4xx
//     -loader BinaryLoader -loader-baseAddr 0x80000000
//     -postScript ExportManifest.java <out.json> [<entry>]
//
// Driven by analyze/ghidra.sh. The manifest holds addresses only, never code.
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.*;

import java.io.PrintWriter;
import java.util.*;

public class ExportManifest extends GhidraScript {

    private static String hx(long v) { return String.format("\"0x%08x\"", v); }
    private static String hx(Address a) { return hx(a.getOffset()); }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args.length > 0 ? args[0] : "manifest.json";
        long entry = args.length > 1 ? Long.decode(args[1]) : 0x80003000L;

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        Memory mem = currentProgram.getMemory();
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        long base = currentProgram.getMinAddress().getOffset();
        long end = currentProgram.getMaxAddress().getOffset() + 1;

        // Seed the boot entry so it is a function even if analysis missed it.
        Address ea = sp.getAddress(entry);
        if (listing.getInstructionAt(ea) == null) {
            new DisassembleCommand(ea, null, true).applyTo(currentProgram, monitor);
        }
        if (fm.getFunctionAt(ea) == null && fm.getFunctionContaining(ea) == null) {
            new CreateFunctionCmd(ea).applyTo(currentProgram, monitor);
        }

        // Code reached only through pointers can be decoded without being made
        // a function (e.g. a callback). Any aligned data word that points at a
        // decoded instruction outside every function becomes a function entry.
        int promoted = 0;
        for (long a = base; a + 4 <= end; a += 4) {
            Address ad = sp.getAddress(a);
            if (listing.getInstructionContaining(ad) != null) continue;   // data words only
            long v = mem.getInt(ad) & 0xFFFFFFFFL;
            if (v < base || v >= end || (v & 3) != 0) continue;
            Address va = sp.getAddress(v);
            if (listing.getInstructionAt(va) == null || fm.getFunctionContaining(va) != null) continue;
            if (new CreateFunctionCmd(va).applyTo(currentProgram, monitor)) promoted++;
        }
        for (int k = 2; k < args.length; k++) {                          // extra entries from game.toml
            Address xa = sp.getAddress(Long.decode(args[k]));
            if (listing.getInstructionAt(xa) == null) new DisassembleCommand(xa, null, true).applyTo(currentProgram, monitor);
            if (fm.getFunctionAt(xa) == null) new CreateFunctionCmd(xa).applyTo(currentProgram, monitor);
        }
        // Decoded code outside every function that nothing falls into is an entry
        // point reached through an address built in code (lis/addi), e.g. an
        // interrupt handler or a callback registered at run time.
        int orphans = 0;
        for (Instruction in : listing.getInstructions(true)) {
            Address ia = in.getAddress();
            if (fm.getFunctionContaining(ia) != null) continue;
            Instruction prev = listing.getInstructionContaining(ia.subtract(1));
            boolean fallsIn = prev != null && prev.getFallThrough() != null && prev.getFallThrough().equals(ia);
            if (fallsIn) continue;
            if (new CreateFunctionCmd(ia).applyTo(currentProgram, monitor)) orphans++;
        }
        println("manifest: promoted " + promoted + " pointer targets and " + orphans + " orphan entries to functions");

        Set<Long> starts = new TreeSet<>();
        for (Function f : fm.getFunctions(true)) starts.add(f.getEntryPoint().getOffset());

        try (PrintWriter w = new PrintWriter(outPath, "UTF-8")) {
            w.println("{");
            w.printf("  \"base\": %s,\n  \"end\": %s,\n  \"entry\": %s,\n", hx(base), hx(end), hx(entry));
            w.println("  \"functions\": [");
            boolean firstF = true;
            for (Function f : fm.getFunctions(true)) {
                AddressSetView body = f.getBody();
                List<String> ranges = new ArrayList<>();
                for (AddressRange r : body) ranges.add("[" + hx(r.getMinAddress()) + "," + hx(r.getMaxAddress().getOffset() + 1) + "]");

                TreeSet<Long> calls = new TreeSet<>(), tails = new TreeSet<>();
                List<String> tables = new ArrayList<>(), indirect = new ArrayList<>(), special = new ArrayList<>();
                int ninsn = 0;
                for (Instruction in : listing.getInstructions(body, true)) {
                    ninsn++;
                    String m = in.getMnemonicString().toLowerCase();
                    long at = in.getAddress().getOffset();
                    if (m.equals("bctr")) {
                        List<String> t = new ArrayList<>();
                        for (Reference r : in.getReferencesFrom())
                            if (r.getReferenceType().isFlow()) t.add(hx(r.getToAddress()));
                        tables.add("{\"at\": " + hx(at) + ", \"targets\": [" + String.join(",", t) + "]}");
                        continue;
                    }
                    if (m.equals("blrl") || m.equals("bctrl") || m.startsWith("bclrl") || m.startsWith("bcctrl")) {
                        indirect.add(hx(at));
                        continue;
                    }
                    if (m.equals("sc") || m.equals("rfi") || m.equals("rfci") || m.startsWith("mtmsr") || m.startsWith("wrtee"))
                        special.add("{\"at\": " + hx(at) + ", \"op\": \"" + m + "\"}");
                    FlowType ft = in.getFlowType();
                    if (!ft.isJump() && !ft.isCall()) continue;
                    for (Address t : in.getFlows()) {
                        long to = t.getOffset();
                        if (ft.isCall()) calls.add(to);
                        else if (!body.contains(t)) tails.add(to);
                    }
                }
                if (!firstF) w.println(",");
                firstF = false;
                w.printf("    {\"entry\": %s, \"name\": \"%s\", \"insns\": %d, \"ranges\": [%s]",
                        hx(f.getEntryPoint()), f.getName(), ninsn, String.join(",", ranges));
                w.print(", \"calls\": [" + join(calls) + "]");
                if (!tails.isEmpty()) w.print(", \"tails\": [" + join(tails) + "]");
                if (!tables.isEmpty()) w.print(", \"jumptables\": [" + String.join(",", tables) + "]");
                if (!indirect.isEmpty()) w.print(", \"indirect\": [" + String.join(",", indirect) + "]");
                if (!special.isEmpty()) w.print(", \"special\": [" + String.join(",", special) + "]");
                w.print("}");
            }
            w.println("\n  ],");

            // Instructions Ghidra decoded outside any function: kernel asm,
            // vectors, or data mis-decoded as code. Reported as ranges.
            w.println("  \"orphan_code\": [");
            boolean firstO = true;
            long rs = -1, re = -1;
            for (Instruction in : listing.getInstructions(true)) {
                if (fm.getFunctionContaining(in.getAddress()) != null) continue;
                long a = in.getAddress().getOffset();
                if (a == re) { re = a + in.getLength(); continue; }
                if (rs >= 0) { w.print((firstO ? "" : ",\n") + "    [" + hx(rs) + "," + hx(re) + "]"); firstO = false; }
                rs = a; re = a + in.getLength();
            }
            if (rs >= 0) w.print((firstO ? "" : ",\n") + "    [" + hx(rs) + "," + hx(re) + "]");
            w.println("\n  ],");

            // Every aligned word in the image (outside function bodies) whose
            // value is a known function entry: candidate indirect-call targets
            // (function-pointer tables, callbacks, thread entry points).
            w.println("  \"code_pointers\": [");
            boolean firstP = true;
            for (long a = base; a + 4 <= end; a += 4) {
                Address ad = sp.getAddress(a);
                if (fm.getFunctionContaining(ad) != null) continue;
                long v = mem.getInt(ad) & 0xFFFFFFFFL;
                if (!starts.contains(v)) continue;
                w.print((firstP ? "" : ",\n") + "    [" + hx(a) + "," + hx(v) + "]");
                firstP = false;
            }
            w.println("\n  ]");
            w.println("}");
        }
        println("manifest: " + fm.getFunctionCount() + " functions -> " + outPath);
    }

    private static String join(Collection<Long> xs) {
        StringBuilder sb = new StringBuilder();
        for (long x : xs) { if (sb.length() > 0) sb.append(','); sb.append(hx(x)); }
        return sb.toString();
    }
}
