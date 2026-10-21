import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;

public class DecompileWithTemporaryFixups extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 4) {
            printerr("Usage: DecompileWithTemporaryFixups <function-entry> <body-end> <nonreturn-entry> <timeout-seconds>");
            return;
        }
        Address entry = currentProgram.getAddressFactory().getAddress(args[0]);
        Address bodyEnd = currentProgram.getAddressFactory().getAddress(args[1]);
        Address nonreturnEntry = currentProgram.getAddressFactory().getAddress(args[2]);
        int timeout = Integer.parseInt(args[3]);
        if (entry == null || bodyEnd == null || nonreturnEntry == null || timeout <= 0) {
            printerr("Invalid address or timeout");
            return;
        }
        Function function = currentProgram.getFunctionManager().getFunctionAt(entry);
        Function nonreturnFunction = currentProgram.getFunctionManager().getFunctionAt(nonreturnEntry);
        if (function == null || nonreturnFunction == null) {
            printerr("Function not found at requested entry");
            return;
        }
        int transaction = currentProgram.startTransaction("Temporary decompiler fixups");
        DecompInterface decompiler = new DecompInterface();
        try {
            nonreturnFunction.setNoReturn(false);
            function.setBody(new AddressSet(entry, bodyEnd));
            decompiler.openProgram(currentProgram);
            println("Function: " + function.getName() + " " + function.getEntryPoint() +
                " body=" + function.getBody().getMinAddress() + ".." + function.getBody().getMaxAddress() +
                " prototype=" + function.getSignature());
            DecompileResults result = decompiler.decompileFunction(function, timeout, monitor);
            if (!result.decompileCompleted() || result.getDecompiledFunction() == null) {
                printerr("Decompilation incomplete: " + result.getErrorMessage());
            } else {
                println(result.getDecompiledFunction().getC());
            }
        } finally {
            decompiler.dispose();
            currentProgram.endTransaction(transaction, false);
            println("Temporary analysis changes rolled back.");
        }
    }
}
