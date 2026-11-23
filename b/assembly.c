#include "vex.h"
#include "assembly.h"
#include "b/b_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/** Assembly manifest inspection and validation command for b.sh. */
;;OVERVIEW
/** MODULE: assembly (vex/b/b/assembly.c). Reads and checks assembly JSON descriptors. */


static utf8 *read_file(const utf8 *path)
{
    FILE *f = fopen(path, "rb");
    if (f == nullptr)
        return nullptr;
    fseek(f, 0, SEEK_END);
    i64 sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0)
    {
        fclose(f);
        return nullptr;
    }
    utf8 *buf = malloc((bytes)sz + 1);
    if (buf == nullptr)
    {
        fclose(f);
        return nullptr;
    }
    bytes rd = fread(buf, 1, (bytes)sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

bool Assembly_validate(const utf8 *assembly_path, const utf8 *workspace_path, FILE *out)
{
    utf8 *asm_buf = read_file(assembly_path);
    if (asm_buf == nullptr)
    {
        fprintf(out, "assembly: cannot read %s\n", assembly_path);
        return false;
    }
    utf8 *ws_buf = read_file(workspace_path);
    if (ws_buf == nullptr)
    {
        fprintf(out, "assembly: cannot read %s\n", workspace_path);
        free(asm_buf);
        return false;
    }

    if (bStrFind(asm_buf, "\"assembly\"") == nullptr)
    {
        fprintf(out, "assembly: missing \"assembly\" object\n");
        free(asm_buf);
        free(ws_buf);
        return false;
    }

    fprintf(out, "assembly: loaded %s\n", assembly_path);
    fprintf(out, "workspace: loaded %s\n", workspace_path);
    fprintf(out, "assembly validation: basic structure OK (full source/dep resolution pending)\n");

    free(asm_buf);
    free(ws_buf);
    return true;
}

i32 Build_assembly(i32 argc, utf8 **argv)
{
    const utf8 *asm_path = argc > 1 ? argv[1] : "b.assembly.json";
    const utf8 *ws_path = argc > 2 ? argv[2] : ".b/workspace.json";
    FILE *out = stdout;
    return Assembly_validate(asm_path, ws_path, out) ? 0 : 1;
}
