import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class ListReferences extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String text : getScriptArgs()) {
            Address target = currentProgram.getAddressFactory().getAddress(text);
            if (target == null) {
                printerr("Unknown address: " + text);
                continue;
            }
            println("=== references to " + target + " ===");
            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(target);
            int count = 0;
            while (references.hasNext() && count < 100 && !monitor.isCancelled()) {
                Reference reference = references.next();
                Function caller = currentProgram.getFunctionManager()
                    .getFunctionContaining(reference.getFromAddress());
                println(reference.getReferenceType() + " @ " + reference.getFromAddress() +
                    " in " + (caller == null ? "<data/no-function>" : caller.getName() + "@" + caller.getEntryPoint()));
                ++count;
            }
            println("references shown: " + count);
        }
    }
}
