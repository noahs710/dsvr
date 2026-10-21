import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.util.Locale;

public class SearchProgramStrings extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] terms = getScriptArgs();
        DataIterator data = currentProgram.getListing().getDefinedData(true);
        int matches = 0;
        while (data.hasNext() && !monitor.isCancelled() && matches < 250) {
            Data item = data.next();
            Object raw = item.getValue();
            if (!(raw instanceof String)) {
                continue;
            }
            String value = (String) raw;
            String folded = value.toLowerCase(Locale.ROOT);
            boolean match = false;
            for (String term : terms) {
                if (!term.isEmpty() && folded.contains(term.toLowerCase(Locale.ROOT))) {
                    match = true;
                    break;
                }
            }
            if (!match) {
                continue;
            }
            println(item.getAddress() + " [" + item.getDataType().getName() + "] " + value);
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(item.getAddress());
            int shown = 0;
            while (refs.hasNext() && shown < 3) {
                Reference ref = refs.next();
                Function owner = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
                println("  " + ref.getReferenceType() + " @ " + ref.getFromAddress() + " in " +
                    (owner == null ? "<data/no-function>" : owner.getName() + "@" + owner.getEntryPoint()));
                ++shown;
            }
            ++matches;
        }
        println("matching strings: " + matches);
    }
}
