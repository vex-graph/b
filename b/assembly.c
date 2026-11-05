#include "assembly.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == nullptr)
        return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0)
    {
        fclose(f);
        return nullptr;
    }
    char *buf = malloc((size_t)sz + 1);
    if (buf == nullptr)
    {
        fclose(f);
        return nullptr;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

bool Assembly_validate(const char *assembly_path, const char *workspace_path, FILE *out)
{
    char *asm_buf = read_file(assembly_path);
    if (asm_buf == nullptr)
    {
        fprintf(out, "assembly: cannot read %s\n", assembly_path);
        return false;
    }
    char *ws_buf = read_file(workspace_path);
    if (ws_buf == nullptr)
    {
        fprintf(out, "assembly: cannot read %s\n", workspace_path);
        free(asm_buf);
        return false;
    }

    /* Basic sanity: assembly must contain "assembly" key. */
    if (strstr(asm_buf, "\"assembly\"") == nullptr)
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

int Build_assembly(int argc, char **argv)
{
    const char *asm_path = argc > 1 ? argv[1] : "b.assembly.json";
    const char *ws_path = argc > 2 ? argv[2] : ".b/workspace.json";
    FILE *out = stdout;
    return Assembly_validate(asm_path, ws_path, out) ? 0 : 1;
}
