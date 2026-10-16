# R6 evidence correction — upstream development fixtures

This addendum does not modify or reseal R6. R6 lifecycle approval, as supplied
by the parent, is `APPROVE R6 SOFTWARE LIFECYCLE`. The immutable R6 manifest is:

`software/.pio/production-ui-evidence-lifecycle-r6/manifest.json`

SHA-256: `464828efe10fc0ee34b49c97c07b6c75ff75f091dda09240c7bb2f41c31b08ab`.

The former blanket statements that the firmware/bundle is literally private-PEM
free are incorrect and must not be used as a distribution assurance. The earlier
scanner did not establish that proposition.

`software/src/modules/iso15118/dev_certs.cpp` contains two ISO15118 development
private-key PEM fixtures. The file is byte-identical to local upstream base
`a114447c4582456d123a0832eaa2d3ce5dd29f88`; SHA-256:
`3f6615e0bf66c47fba1fd4cff2ba01f494461306de70e092f9672603d116f9e4`.
They remain unchanged upstream source/build content. No payload is reproduced
in this addendum. R7 inventories the file and fixture count explicitly.

The precise intended boundary is no customer credentials, customer/private
Tesla key material, user-specific identifiers, request dumps, UART captures,
full-flash backups or private/hardware evidence in the review handoff. This is
not a claim that upstream SDK/test trees or firmware contain no development
cryptographic fixtures. Unchanged public development fixtures are not production
credentials and are not a cryptographic, licensing or deployment approval.

R0–R6 evidence remains byte-for-byte preserved. R7 is a separate software
packaging tranche, not a retrospective correction of the R6 manifest.
