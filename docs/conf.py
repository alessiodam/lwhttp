import pathlib
import re

root = pathlib.Path(__file__).resolve().parent.parent
header = (root / "lwhttp.h").read_text()

project = "LWHTTP"
author = "Alessio D'Ambrosio"
copyright = "2026, Alessio D'Ambrosio"
release = version = re.search(r'#define LWHTTP_VERSION_STRING "([^"]+)"', header).group(1)

extensions = [
    "myst_parser",
    "sphinx_design",
    "sphinx_copybutton",
    "sphinxext.opengraph",
    "sphinx_sitemap",
    "hawkmoth",
]

hawkmoth_root = str(root)
hawkmoth_clang = ["-std=c11", "-DLWHTTP_DOCS"]

myst_enable_extensions = ["colon_fence", "fieldlist", "deflist", "attrs_inline", "attrs_block", "substitution"]
myst_heading_anchors = 3
myst_substitutions = {"version": release}

primary_domain = "c"
highlight_language = "c"
exclude_patterns = ["_build", "requirements.txt", "check_exports.py"]

html_theme = "shibuya"
html_title = "LWHTTP"
html_baseurl = "https://lwhttp-docs.alessiodam.dev/"
html_copy_source = False
html_show_sourcelink = False
html_context = {
    "source_type": "github",
    "source_user": "alessiodam",
    "source_repo": "lwhttp",
    "source_version": "main",
    "source_docs_path": "/docs/",
}
html_theme_options = {
    "accent_color": "indigo",
    "dark_code": True,
    "github_url": "https://github.com/alessiodam/lwhttp",
    "show_ai_links": False,
    "globaltoc_expand_depth": 1,
    "nav_links": [
        {"title": "Guide", "url": "guide/index"},
        {"title": "API", "url": "api/index"},
        {"title": "lwIP-CE", "url": "https://cagscalclabs.github.io/lwip-ce/"},
    ],
}

sitemap_url_scheme = "{link}"
ogp_site_url = html_baseurl
ogp_description_length = 200
ogp_social_cards = {"enable": False}
copybutton_exclude = ".linenos, .gp"
