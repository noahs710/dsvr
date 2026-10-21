import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Instruction;

// Query-only overlap repair. The EXE and project bytes are never changed.
public class PhysicalDamageEvidence extends GhidraScript {
    public void run() throws Exception {
        int tx = currentProgram.startTransaction("Temporary overlapping instruction repair");
        try {
            for (String text : getScriptArgs()) {
                Address at = toAddr(text);
                if (getInstructionAt(at) == null) {
                    Instruction containing = getInstructionContaining(at);
                    if (containing != null) clearListing(containing.getMinAddress(), at.add(15));
                    else clearListing(at, at.add(15));
                    new DisassembleCommand(at, new AddressSet(at, at.add(15)), false)
                        .applyTo(currentProgram, monitor);
                    println("Exact branch target " + at + " -> " + getInstructionAt(at));
                }
            }
            runScript("RecoverSplitFunction.java", getScriptArgs());
        } finally {
            currentProgram.endTransaction(tx, false);
            println("Overlap listing repair rolled back.");
        }
    }
}
