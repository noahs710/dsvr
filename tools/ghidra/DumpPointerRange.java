import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;

public class DumpPointerRange extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("Usage: DumpPointerRange <start-address> <byte-length> [<start-address> <byte-length> ...]");
            return;
        }
        for (int pair = 0; pair + 1 < args.length; pair += 2) {
            Address start = currentProgram.getAddressFactory().getAddress(args[pair]);
            long length = Long.decode(args[pair + 1]);
            if (start == null || length < 8) {
                printerr("Invalid range: " + args[pair] + " " + args[pair + 1]);
                continue;
            }
            println("=== pointers " + start + " .. " + start.add(length - 1L) + " ===");
            for (long offset = 0; offset + 8 <= length && !monitor.isCancelled(); offset += 8) {
                Address at = start.add(offset);
                try {
                    long value = currentProgram.getMemory().getLong(at);
                    Address pointer = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
                    Function function = currentProgram.getFunctionManager().getFunctionAt(pointer);
                    Symbol symbol = currentProgram.getSymbolTable().getPrimarySymbol(pointer);
                    println(at + " -> " + pointer + " " +
                        (function == null ? "" : "function=" + function.getName()) +
                        (symbol == null ? "" : " symbol=" + symbol.getName()));
                } catch (Exception error) {
                    println(at + " -> <unreadable: " + error.getMessage() + ">");
                }
            }
        }
    }
}
