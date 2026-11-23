# Contributing documentation

Handwritten pages live in `docs`. CI generates the engine API page from the
registered API with `nomadc docs`; do not edit the generated Markdown.
The site uses Material for MkDocs.

## Build locally

Use an extracted SDK matching the source revision, or the directory containing
the locally built tools and templates. From the repository root:

```console
python scripts/generate_documentation.py --sdk <sdk-directory>
python -m pip install -r docs/requirements.txt
python -m mkdocs build --strict
```

The generated Markdown is in `out/documentation/source`; the rendered site is
in `out/documentation/site`.
Run `python -m mkdocs serve` to preview it locally.

For API generation outside the site workflow, see the
[compiler CLI](cli.md#compiler).

## CI and deployment

Pull requests validate the site without deploying it. Generation or strict
site-build errors fail validation.

After successful main-branch push CI, the Pages workflow deploys the validated
site artifact. In repository Settings, select **Pages > Build and deployment >
Source > GitHub Actions** before the first deployment.

The site documents the main branch. Release-versioned documentation is not
currently provided.
