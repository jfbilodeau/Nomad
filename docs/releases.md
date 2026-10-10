# Publishing an SDK release

SDK releases use tags in the form `v<major>.<minor>.<patch>`, such as `v0.1.0`.
The tag must match the version declared in the root `CMakeLists.txt`.
Prerelease suffixes are not currently supported.

1. Set the project version, commit the changes, and ensure the normal CI passes.
2. Tag the intended commit and push the tag:

   ```console
   git tag v0.1.0
   git push origin v0.1.0
   ```

3. The Release workflow runs the existing Windows and Ubuntu build, test, and
   isolated-package checks against the tagged source.
4. After all checks pass, it creates a draft GitHub Release containing:
   - `nomad-sdk-windows-x64-<version>.zip`
   - `nomad-sdk-linux-x64-<version>.zip`
   - `SHA256SUMS.txt`, containing checksums for both ZIPs
   - `install.ps1` and `install.sh`, the Windows and Linux bootstrap installers
5. Download the draft assets, verify their checksums, and check the SDK and a
   packaged game on both platforms. Review the generated release notes, then
   publish the draft manually.

Smoke-test packages and validation scripts remain CI artifacts; they are not
attached to the release. Each SDK ZIP includes the runtime.

If validation fails, no draft release is created. Fix the failure before
retrying. Draft creation fails if a release already exists for the tag rather
than silently replacing its assets; inspect the existing release before
rerunning that step.
