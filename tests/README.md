# Tests

Run python3 -m unittest discover -s tests -v in WSL. The build runs these after linking.

The suite covers actual SCSI and transfer code with a fake card/USB transport and
address/undefined-behavior sanitizers, parser and ownership gates, helper identity,
artifact corruption/board checks and protected source synchronization.

Artifact tests require a previously generated build. The build command generates one
before testing and independently validates the current artifacts before export.

These are software tests. The remaining hardware matrix is ../docs/TESTING.md.
