"""Package this dependency-free language extension for VS Code."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import json

root = Path(__file__).resolve().parent
meta = json.loads((root / "package.json").read_text(encoding="utf-8"))
name = f"{meta['name']}-{meta['version']}.vsix"
manifest = f'''<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Id="{meta['name']}" Version="{meta['version']}" Publisher="{meta['publisher']}" />
    <DisplayName>{meta['displayName']}</DisplayName>
    <Description xml:space="preserve">{meta['description']}</Description>
    <Tags>mxpl,language</Tags>
    <Categories>Programming Languages</Categories>
  </Metadata>
  <Installation><InstallationTarget Id="Microsoft.VisualStudio.Code" Version="[1.80.0,)" /></Installation>
  <Dependencies />
  <Assets><Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" /></Assets>
</PackageManifest>'''
content_types = '''<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="json" ContentType="application/json" />
  <Default Extension="vsixmanifest" ContentType="text/xml" />
</Types>'''
out = root / name
with ZipFile(out, "w", ZIP_DEFLATED) as package:
    package.writestr("extension.vsixmanifest", manifest)
    package.writestr("[Content_Types].xml", content_types)
    for path in [root / "package.json", root / "language-configuration.json", root / "syntaxes/mxpl.tmLanguage.json"]:
        package.write(path, "extension/" + path.relative_to(root).as_posix())
print(out)
