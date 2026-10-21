import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class DumpInstructions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("Usage: DumpInstructions <start-address> <byte-length>");
            return;
        }
        Address start = currentProgram.getAddressFactory().getAddress(args[0]);
        int length = Integer.decode(args[1]);
        if (start == null || length <= 0) {
            printerr("Invalid address range");
            return;
        }
        Address end = start.add(length - 1L);
        println("=== instructions " + start + " .. " + end + " ===");
        InstructionIterator instructions = currentProgram.getListing().getInstructions(start, true);
        while (instructions.hasNext() && !monitor.isCancelled()) {
            Instruction instruction = instructions.next();
            if (instruction.getAddress().compareTo(end) > 0) break;
            println(instruction.getAddress() + "  " + instruction);
        }
    }
}
