import argparse
import csv
import xml.etree.ElementTree as ET

REGADDR_SUFFIX = "_RegAddr"


def _local_name(tag: str) -> str:
    if "}" in tag:
        return tag.rsplit("}", 1)[1]
    return tag


def _first_child_text(element: ET.Element, child_local_tag: str) -> str | None:
    for child in element:
        if _local_name(child.tag) == child_local_tag and child.text is not None:
            text = child.text.strip()
            if text:
                return text
    return None


def iter_regaddr_definitions(root: ET.Element, strip_suffix: bool):
    for element in root.iter():
        if _local_name(element.tag) != "Group":
            continue
        if element.attrib.get("Comment") != "RegAddr":
            continue

        for integer_node in element.iter():
            if _local_name(integer_node.tag) != "Integer":
                continue
            name = integer_node.attrib.get("Name")
            if not name:
                continue
            value = _first_child_text(integer_node, "Value")
            if not value:
                continue

            out_name = name
            if strip_suffix and out_name.endswith(REGADDR_SUFFIX):
                out_name = out_name[: -len(REGADDR_SUFFIX)]
            yield {
                "Name": out_name,
                "Address": value,
                "Source": "RegAddrGroup",
                "XmlTag": _local_name(integer_node.tag),
                "RawName": name,
            }


def iter_inline_addresses(root: ET.Element):
    for element in root.iter():
        name = element.attrib.get("Name")
        if not name:
            continue
        address = _first_child_text(element, "Address")
        if not address:
            continue
        yield {
            "Name": name,
            "Address": address,
            "Source": "InlineAddress",
            "XmlTag": _local_name(element.tag),
            "RawName": name,
        }


def xml_to_csv(xml_file: str, csv_file: str, *, strip_suffix: bool, only_regaddr: bool) -> int:
    tree = ET.parse(xml_file)
    root = tree.getroot()

    rows = list(iter_regaddr_definitions(root, strip_suffix=strip_suffix))
    if not only_regaddr:
        rows.extend(iter_inline_addresses(root))

    rows.sort(key=lambda r: (r["Name"], r["Source"], r["Address"]))

    with open(csv_file, "w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(
            file,
            fieldnames=["Name", "Address", "Source", "XmlTag", "RawName"],
        )
        writer.writeheader()
        writer.writerows(rows)

    return len(rows)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract register addresses from GenICam XML into CSV.",
    )
    parser.add_argument(
        "-i",
        "--input",
        default="modules/xml/Hikrobot_Smart_Device_Profile.xml",
        help="Input XML file path.",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="regaddr.csv",
        help="Output CSV file path.",
    )
    parser.add_argument(
        "--no-strip-regaddr-suffix",
        action="store_true",
        help=f"Keep the '{REGADDR_SUFFIX}' suffix in names from the RegAddr group.",
    )
    parser.add_argument(
        "--only-regaddr",
        action="store_true",
        help="Only export <Group Comment='RegAddr'>/<Integer> address definitions.",
    )

    args = parser.parse_args()

    count = xml_to_csv(
        args.input,
        args.output,
        strip_suffix=not args.no_strip_regaddr_suffix,
        only_regaddr=args.only_regaddr,
    )
    print(f"Wrote {count} rows to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
