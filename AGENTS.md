# Development guidelines

- Read README.md and docs/DEVELOPMENT.md before changes.
- Scope: a dedicated CubeOrange+ SD reader preserving the installed bootloader.
- Edit firmware/ and tools/; never patch tracked upstream files in place.
- Preserve dependency pins and original license notices; follow upstream AGENTS.md for integrated code.
- Builds must not flash hardware. Hardware tests require an explicitly selected bench device.
- Run python3 tools/dev.py build after firmware changes; it includes host tests and artifact validation.
- Keep private device data, parameter dumps, logs and generated artifacts in ignored work/ or artifacts/.
- Distinguish automated build validation from measured hardware results in docs/TESTING.md.
