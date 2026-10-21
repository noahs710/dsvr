import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;

public class DumpPointerRange extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("Usage: DumpPointerRange <start-address> <byte-length>");
            return;
        }
        Address start = currentProgram.getAddressFactory().getAddress(args[0]);
        long length = Long.decode(args[1]);
        if (start == null || length < 8) {
            printerr("Invalid range");
            return;
        }
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
