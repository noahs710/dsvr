// Headless, read-only Ghidra probe for the pinned Dark Souls Remastered build.
// Usage through analyzeHeadless:
//   -postScript DsvrProbe.java decompile 1403549e0
//   -postScript DsvrProbe.java xrefs 1403549e0
//   -postScript DsvrProbe.java qwords 141325708
//   -postScript DsvrProbe.java refscan 141325708
//   -postScript DsvrProbe.java symbol XInputGetState
//   -postScript DsvrProbe.java string PadManipulator

import java.util.Iterator;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Program;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.data.Composite;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeComponent;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class DsvrProbe extends GhidraScript {
    private static String normalizeAddress(String raw) {
        if (raw.startsWith("0x") || raw.startsWith("0X")) return raw.substring(2);
        return raw;
    }

    private void printXrefs(Address target) {
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(target);
        int count = 0;
        while (refs.hasNext()) {
            Reference ref = refs.next();
            Function owner = currentProgram.getFunctionManager()
                .getFunctionContaining(ref.getFromAddress());
            println("XREF " + ref.getFromAddress() + " -> " + target +
                " type=" + ref.getReferenceType() + " owner=" +
                (owner == null ? "<none>" : owner.getName() + "@" + owner.getEntryPoint()));
            count++;
        }
        println("XREF_COUNT " + count);
    }

    private void decompile(String rawAddress) throws Exception {
        Address address = currentProgram.getAddressFactory().getDefaultAddressSpace()
            .getAddress(normalizeAddress(rawAddress));
        Function function = currentProgram.getFunctionManager().getFunctionContaining(address);
        if (function == null) {
            println("NO_FUNCTION " + address);
            return;
        }
        println("FUNCTION " + function.getName() + " entry=" + function.getEntryPoint() +
            " body=" + function.getBody());
        DecompInterface decompiler = new DecompInterface();
        try {
            decompiler.openProgram(currentProgram);
            DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
            if (!result.decompileCompleted()) {
                println("DECOMPILE_FAILED " + result.getErrorMessage());
                return;
            }
            println(result.getDecompiledFunction().getC());
        } finally {
            decompiler.dispose();
        }
    }

    private void decompileMany(String rawAddresses) throws Exception {
        for (String address : rawAddresses.split(",")) decompile(address.trim());
    }

    private void findSymbol(String needle) {
        SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
        int count = 0;
        while (symbols.hasNext()) {
            Symbol symbol = symbols.next();
            if (!symbol.getName().toLowerCase().contains(needle.toLowerCase())) continue;
            println("SYMBOL " + symbol.getName(true) + " @ " + symbol.getAddress());
            printXrefs(symbol.getAddress());
            count++;
        }
        println("SYMBOL_COUNT " + count);
    }

    private void findString(String needle) {
        DataIterator data = currentProgram.getListing().getDefinedData(true);
        int count = 0;
        while (data.hasNext()) {
            Data item = data.next();
            if (!item.hasStringValue()) continue;
            String value = String.valueOf(item.getValue());
            if (!value.toLowerCase().contains(needle.toLowerCase())) continue;
            println("STRING " + item.getAddress() + " " + value);
            printXrefs(item.getAddress());
            count++;
        }
        println("STRING_COUNT " + count);
    }

    private void findXrefs(String rawAddress) throws Exception {
        Address address = currentProgram.getAddressFactory().getDefaultAddressSpace()
            .getAddress(normalizeAddress(rawAddress));
        printXrefs(address);
    }

    private void dumpQwords(String rawAddress) throws Exception {
        Address center = currentProgram.getAddressFactory().getDefaultAddressSpace()
            .getAddress(normalizeAddress(rawAddress));
        Address cursor = center.subtract(0x80);
        for (int i = 0; i < 33; ++i, cursor = cursor.add(8)) {
            long value = currentProgram.getMemory().getLong(cursor);
            Address target = currentProgram.getAddressFactory().getDefaultAddressSpace()
                .getAddress(value);
            Function function = currentProgram.getFunctionManager().getFunctionAt(target);
            int refs = 0;
            ReferenceIterator iterator = currentProgram.getReferenceManager().getReferencesTo(cursor);
            while (iterator.hasNext()) { iterator.next(); refs++; }
            println("QWORD " + cursor + " = " + target +
                (function == null ? "" : " " + function.getName()) + " refs=" + refs);
        }
    }

    private void scanReferences(String rawAddress) throws Exception {
        Address center = currentProgram.getAddressFactory().getDefaultAddressSpace()
            .getAddress(normalizeAddress(rawAddress));
        Address cursor = center.subtract(0x1000);
        for (int i = 0; i < 1025; ++i, cursor = cursor.add(8)) {
            int refs = 0;
            StringBuilder owners = new StringBuilder();
            ReferenceIterator iterator = currentProgram.getReferenceManager().getReferencesTo(cursor);
            while (iterator.hasNext()) {
                Reference ref = iterator.next();
                Function owner = currentProgram.getFunctionManager()
                    .getFunctionContaining(ref.getFromAddress());
                if (refs < 4) owners.append(" ").append(ref.getFromAddress()).append("@").append(
                    owner == null ? "<data>" : owner.getName());
                refs++;
            }
            if (refs > 0) println("REFSCAN " + cursor + " refs=" + refs + owners);
        }
    }

    private void findType(String needle) {
        Iterator<DataType> types = currentProgram.getDataTypeManager().getAllDataTypes();
        int count = 0;
        while (types.hasNext()) {
            DataType type = types.next();
            if (!type.getPathName().toLowerCase().contains(needle.toLowerCase())) continue;
            println("TYPE " + type.getPathName() + " length=" + type.getLength());
            if (type instanceof Composite) {
                Composite composite = (Composite) type;
                for (DataTypeComponent component : composite.getComponents()) {
                    println("  FIELD +0x" + Integer.toHexString(component.getOffset()) +
                        " " + component.getFieldName() + " : " +
                        component.getDataType().getPathName() + " len=" + component.getLength());
                }
            }
            if (++count >= 20) break;
        }
        println("TYPE_COUNT " + count);
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("USAGE DsvrProbe.java <decompile|xrefs|qwords|refscan|symbol|string|type> <query>");
            return;
        }
        println("PROGRAM " + currentProgram.getName() + " imageBase=" +
            currentProgram.getImageBase());
        if (args[0].equalsIgnoreCase("decompile")) {
            for (int i = 1; i < args.length; ++i) decompileMany(args[i]);
        }
        else if (args[0].equalsIgnoreCase("xrefs")) findXrefs(args[1]);
        else if (args[0].equalsIgnoreCase("qwords")) dumpQwords(args[1]);
        else if (args[0].equalsIgnoreCase("refscan")) scanReferences(args[1]);
        else if (args[0].equalsIgnoreCase("type")) findType(args[1]);
        else if (args[0].equalsIgnoreCase("symbol")) findSymbol(args[1]);
        else if (args[0].equalsIgnoreCase("string")) findString(args[1]);
        else println("UNKNOWN_MODE " + args[0]);
    }
}
