// Export static decompilation and cross-references without executing the target.
// @category Kisaku
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;

public class ExportNative extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length!=1)throw new IllegalArgumentException("output directory required");
        Path output=Paths.get(args[0]);Files.createDirectories(output);
        DecompInterface decompiler=new DecompInterface();
        try {
            if(!decompiler.openProgram(currentProgram))throw new IOException(decompiler.getLastMessage());
            try(BufferedWriter index=Files.newBufferedWriter(output.resolve("functions.tsv"),StandardCharsets.UTF_8)) {
                index.write("address\tname\tbytes\tstatus\n");
                FunctionIterator it=currentProgram.getFunctionManager().getFunctions(true);
                int count=0;
                while(it.hasNext()&&!monitor.isCancelled()) {
                    Function f=it.next();if(f.isExternal())continue;
                    String address=f.getEntryPoint().toString();
                    DecompileResults result=decompiler.decompileFunction(f,30,monitor);
                    boolean ok=result.decompileCompleted()&&result.getDecompiledFunction()!=null;
                    String contents=ok?result.getDecompiledFunction().getC():"/* "+result.getErrorMessage()+" */\n";
                    Files.writeString(output.resolve(address+".c"),contents,StandardCharsets.UTF_8);
                    index.write(address+"\t"+f.getName()+"\t"+f.getBody().getNumAddresses()+"\t"+(ok?"ok":"failed")+"\n");
                    if(++count%200==0){index.flush();println("Exported "+count+" functions");}
                }
                println("Static export complete: "+count+" functions");
            }
        } finally {decompiler.dispose();}
    }
}
