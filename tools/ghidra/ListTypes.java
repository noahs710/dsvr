import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeComponent;
import ghidra.program.model.data.Structure;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public class ListTypes extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] terms = getScriptArgs();
        List<DataType> types = new ArrayList<>();
        currentProgram.getDataTypeManager().getAllDataTypes(types);
        int matches = 0;
        for (DataType type : types) {
            String name = type.getName();
            String folded = name.toLowerCase(Locale.ROOT);
            boolean match = terms.length == 0;
            for (String term : terms) {
                if (!term.isEmpty() && folded.contains(term.toLowerCase(Locale.ROOT))) {
                    match = true;
                    break;
                }
            }
            if (!match) continue;
            println(type.getCategoryPath() + "/" + name + " size=" + type.getLength() +
                " kind=" + type.getClass().getSimpleName());
            if (type instanceof Structure) {
                Structure structure = (Structure) type;
                for (DataTypeComponent component : structure.getComponents()) {
                    println("  +" + Integer.toHexString(component.getOffset()) + " " +
                        component.getFieldName() + ": " + component.getDataType().getName() +
                        "[" + component.getLength() + "]");
                }
            }
            if (++matches >= 200) break;
        }
        println("matching types: " + matches + " of " + types.size());
    }
}
