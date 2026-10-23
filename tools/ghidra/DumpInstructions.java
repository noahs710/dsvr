import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class DumpInstructions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("Usage: DumpInstructions <start-address> <byte-length> [<start-address> <byte-length> ...]");
            return;
        }
        for (int pair = 0; pair + 1 < args.length; pair += 2) {
            Address start = currentProgram.getAddressFactory().getAddress(args[pair]);
            int length = Integer.decode(args[pair + 1]);
            if (start == null || length <= 0) {
                printerr("Invalid address range: " + args[pair] + " " + args[pair + 1]);
                continue;
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
}
