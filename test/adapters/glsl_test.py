"""GLSL owner: literal native invocations plus real SPIR-V where tools exist.

Fake tools prove shaderc argv, errors and preservation, not shader compilation.
Real glslc/glslang tests independently check the binary magic and malformed input.
No GPU execution or arbitrary caller filesystem mutation is claimed.
"""
import shutil
import struct
from pathlib import Path
from adapter_support import AdapterCase


class GlslTest(AdapterCase):
    def test_shaderc_invocation_generation_preservation_and_no_host_run(self):
        source = self.source("main.comp", "shader input")
        tool = self.source("fake glslc ; literal", "#!/usr/bin/env python3\n"
                           "import pathlib,sys\n"
                           "assert sys.argv[1:3]==['--target-env=vulkan1.0','-Werror']\n"
                           "p=pathlib.Path(sys.argv[3])\n"
                           "o=pathlib.Path(sys.argv[5])\n"
                           "assert sys.argv[4]=='-o'\n"
                           "o.write_bytes(b'partial' if 'broken' in p.read_text() else b'compiled')\n"
                           "sys.exit(8 if 'broken' in p.read_text() else 0)\n")
        tool.chmod(0o755)
        env = dict(self.environment, GLSLC=str(tool), GLSL_BACKEND="shaderc")
        output = Path(self.invoke("build", "glsl", self.project, environment=env).stdout.strip())
        artifact = output / "main.comp.spv"
        self.assertEqual(artifact.read_bytes(), b"compiled")
        source.write_text("broken")
        self.invoke("build", "glsl", self.project, environment=env, expected=8)
        self.assertEqual(artifact.read_bytes(), b"compiled")
        self.assertEqual(list(output.parent.glob("glsl-*")), [output])
        source.write_text("recovered")
        self.invoke("build", "glsl", self.project, environment=env)
        for mode in ("exec", "instance"):
            self.assertIn("build-only", self.invoke("run", mode, source, expected=None).stderr)
        self.invoke("build", "glsl", self.project, environment=dict(env, GLSLC="missing-glslc"), expected=None)
        self.invoke("build", "glsl", self.project, environment=dict(env, GLSL_BACKEND="bad"), expected=None)
        (self.project / "alias.frag").symlink_to(source)
        self.assertIn("non-symlink", self.invoke("build", "glsl", self.project, environment=env, expected=None).stderr)

    def test_real_shaderc_spirv_and_generic_stage(self):
        self.real_compile("shaderc", "glslc")

    def test_real_glslang_spirv_and_include_failure(self):
        self.real_compile("glslang", "glslangValidator")

    def real_compile(self, backend, tool):
        if shutil.which(tool) is None:
            self.skipTest(f"{tool} unavailable; corresponding real compilation unproved")
        self.source("main.vert", "#version 450\nvoid main(){gl_Position=vec4(0.0);}\n")
        self.source("main.frag", "#version 450\nlayout(location=0) out vec4 c;void main(){c=vec4(1.0);}\n")
        self.source("main.geom", "#version 450\nlayout(points) in;layout(points,max_vertices=1) out;\n"
                    "void main(){gl_Position=gl_in[0].gl_Position;EmitVertex();EndPrimitive();}\n")
        self.source("main.tesc", "#version 450\nlayout(vertices=3) out;\n"
                    "void main(){gl_out[gl_InvocationID].gl_Position=gl_in[gl_InvocationID].gl_Position;}\n")
        self.source("main.tese", "#version 450\nlayout(triangles) in;void main(){gl_Position=gl_in[0].gl_Position;}\n")
        self.source("generic.comp.glsl", "#version 450\n#pragma shader_stage(compute)\nlayout(local_size_x=1) in;void main(){}\n")
        source = self.source("main.comp", "#version 450\nlayout(local_size_x=1) in;void main(){}\n")
        env = dict(self.environment, GLSL_BACKEND=backend)
        output = Path(self.invoke("build", "glsl", self.project, environment=env).stdout.strip())
        for name in ("main.vert", "main.frag", "main.comp", "main.geom", "main.tesc", "main.tese", "generic.comp.glsl"):
            self.assertEqual(struct.unpack("<I", (output / (name + ".spv")).read_bytes()[:4])[0], 0x07230203)
        plan = self.invoke("build", "workspace", self.project, "--plan", environment=env)
        self.assertIn("1 build units", plan.stdout)
        self.assertIn("1 succeeded", self.invoke("build", "workspace", self.project, environment=env).stdout)
        source.write_text('#version 450\n#include "missing.glsl"\n')
        self.invoke("build", "glsl", self.project, environment=env, expected=None)
        self.assertTrue((output / "main.comp.spv").is_file())
        source.write_text("#version 450\nlayout(local_size_x=1) in;void main(){}\n")
        if backend == "shaderc":
            self.source("generic.glsl", "#version 450\n#pragma shader_stage(compute)\nlayout(local_size_x=1) in;void main(){}\n")
        self.invoke("build", "glsl", self.project, environment=env)

    def test_empty_build_rejects(self):
        self.invoke("build", "glsl", self.project, expected=None)


if __name__ == "__main__":
    import unittest
    unittest.main(verbosity=2)
