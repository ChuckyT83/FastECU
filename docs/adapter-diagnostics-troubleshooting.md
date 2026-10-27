# Arrival diagnostic troubleshooting

| Observation | Meaning / next action |
|---|---|
| No registrations | Discovery ran; a J2534 interface is not installed/registered in the queried views. Review the correct vendor installer. No adapter functionality was tested. |
| Registered DLL absent | Registration and filesystem disagree. Preserve the report; review/repair only through the reviewed vendor package. |
| x86/x64 mismatch | Use matching diagnostic bitness. Registry view alone is not DLL architecture. The full GUI remains x64; the diagnostic is not a transport bridge. |
| Malformed/unknown PE | Architecture/loadability is unestablished. Do not try loading it to “see what happens.” Preserve original file/hash for review. |
| Unsigned / offline signature not validated | Not proof of malware or compatibility. Verify source and exact package; offline checks cannot establish current revocation status. |
| USB not shown by default | Candidate descriptions are a filter, not identity proof. Inspect Device Manager and explicitly use `--usb-all` when appropriate. Unknown clone IDs are not assumed to match genuine hardware. |
| USB seen but no J2534 entry | USB recognition and user-mode J2534 installation are separate. Record VID/PID, provider/version/problem code without assuming driver functionality. |
| Device worker refused | Review required flags, selected ID/hash/API and architecture. Clone/OpenPort is intentionally passive-only. Do not bypass by changing labels. |
| DLL load Windows error 126/193 | A reviewed dependency may be absent, the image invalid, or its architecture wrong. The exact raw code is retained. Do not copy random DLLs into system folders or substitute another vendor's driver. |
| Open error | No valid acquired device is assumed; no Close is issued for a failed Open. Stop and review raw status. |
| Version failed/empty/unterminated/API mismatch | Device open and metadata success differ. Cleanup is still attempted once after a successful Open; do not call this compatibility success. |
| Close failed | Cleanup/device state is unconfirmed. No retry/reset is invented. Stop and review. |
| Parent timeout / worker crash | Call/device state is uncertain. No forced termination, concurrent Close/unload or automatic retry is performed. A late collection is review-only. |
| Export error | Report path may not exist or may already be present. Choose a new filename; stdout retains the structured failure. |
| Unexpected update/session/CAN activity | Stop. This toolkit never requests it; vendor initialization behavior must be reviewed. Do not connect to a vehicle or flash firmware to work around it. |

Keep both adapters disconnected from any vehicle. Do not launch the full GUI's
ROM Read as a diagnostic substitute. For clones, preserve supplied packages for
Task 007; genuine Tactrix software/firmware is not automatically appropriate.

No physical adapter was qualified by native mock testing. When sharing a report,
review free-form driver/version descriptions despite default path/instance
redaction. The toolkit never requests VIN, calibration data or device-serial
IOCTLs. An instance ID observed through Windows can contain a serial suffix;
that suffix is removed unless private-detail export was explicitly requested.
