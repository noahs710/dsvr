import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DumpFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try {
            for (String text : getScriptArgs()) {
                Address address = currentProgram.getAddressFactory().getAddress(text);
                if (address == null) {
                    printerr("Unknown address: " + text);
                    continue;
                }
                Function function = currentProgram.getFunctionManager().getFunctionContaining(address);
                if (function == null) {
                    printerr("No function contains " + text);
                    continue;
                }
                println("=== " + function.getName() + " @ " + function.getEntryPoint() + " ===");
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                if (!result.decompileCompleted() || result.getDecompiledFunction() == null) {
                    printerr("Could not decompile " + function.getEntryPoint());
                    continue;
                }
                println(result.getDecompiledFunction().getC());
            }
        } finally {
            decompiler.dispose();
        }
    }
}
