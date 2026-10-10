# Contributing documentation

Handwritten pages live in `docs`. The engine API page is generated from the
registered API with `nomadc docs`; do not edit the generated Markdown.

## Build locally

Use an extracted SDK matching the source revision, or the directory containing
the locally built tools and templates. From the repository root:

```console
python scripts/generate_documentation.py --sdk <sdk-directory>
python -m pip install -r docs/requirements.txt
python -m mkdocs build --strict
```

The preparation script copies handwritten pages to `out/documentation/source`,
initializes a disposable project, and generates `engine-api.md` without
executing game scripts. The rendered site is in `out/documentation/site`.
Run `python -m mkdocs serve` to preview it locally.

The preparation script uses only the Python standard library and needs no
CMake installation. Test it with
`python -m unittest discover -s scripts -p "test_*.py"`.

## CI and deployment

CI reuses the tested Linux SDK artifact; documentation does not trigger another
native build. Generation errors or strict site-build errors fail the job.
Pull requests build the site but do not deploy it.

After successful main-branch push CI, the Pages workflow deploys the validated
site artifact. In repository Settings, select **Pages > Build and deployment >
Source > GitHub Actions** before the first deployment.

The site documents the main branch. Release-versioned documentation is not
currently provided.
