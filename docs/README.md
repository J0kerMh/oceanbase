# Build documentation with MkDocs

OceanBase documentation is built with [MkDocs](https://www.mkdocs.org/). You can check [`mkdocs.yml`](mkdocs.yml) for more information.
Please install MkDocs according to [the installation documents of MkDocs](https://www.mkdocs.org/user-guide/installation/)

## Requirements

Before installing dependencies, please make sure you have installed a recent version of Python 3 and pip.

Then you can run the following command in your terminal at current directory:

```
$ pip install -r requirements.txt
```
## Build the documentation

You can build the documentation by running the following command:

```
$ mkdocs build
```

This will create a new directory to store the output files, which is `site/` by default.

## Start a server locally

You can start a server locally by running the following command:

```
$ mkdocs serve
```
Open up http://127.0.0.1:8000/ in your browser, and you'll see the default home page.

## Modify pages

### Edit a page

If you want to modify the content of a page, you can edit the markdown file in `docs/` directory directly.

### Modify the layout of pages

To modify the layout of pages, you need to edit `mkdocs.yml`.

For configuration details, see [MkDocs User Guide](https://www.mkdocs.org/user-guide/configuration/).

Note the following rules when editing documents:
- All paths in `nav` must be relative to the `docs_dir`, which is `docs` by default. So here `./` is equivalent to [docs](docs).
- All internal links must be relative paths, as MkDocs only supports regular Markdown linking syntax.

## Contribute

See [How to contribute](wiki/how_to_contribute)
