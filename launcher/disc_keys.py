"""Catalog identities for supported encrypted PS3 disc images."""
from dataclasses import dataclass
import hashlib
from http.client import HTTPException
from urllib.request import urlopen


@dataclass(frozen=True)
class DiscKey:
    title_id: str
    key_sha256: str


# Redump disc records 76597, 32130, 58234 and 33376. The key digests
# match both Redump's PS3 disc key set and the pinned TSV below.
DISCS = {
    (8191868928, "888beae930fbf70ae3d832c90a3b5462ee248c2f"):
        DiscKey("BCES00647", "9fde816637018a695933544bfadf6210101ffbdc169994cd08860595b7cfdd17"),
    (8910471168, "9f7f544ab6f856f05fd36647f6096ac13c50cfce"):
        DiscKey("BCES00644", "509c815fabb7bd051f27d968f7f4524d0810b90c968d6935ea2f65e6c384cb01"),
    (5513150464, "bd129c379915c649d10fe7728f15bce9fc4c0733"):
        DiscKey("BCES00645", "27c6e4e500b7f57f2dbee4ba09a35d8daecc7084b4a6446de9208305dee684d8"),
    (2112487424, "2e9814388f3bf1816d07d2792f8c390b9d51dc8a"):
        DiscKey("BCUS98209", "6152fba83463a23c847d2af3795da8e5f6da47466eff652e8fc31349acc004c9"),
}

CATALOG_URL = ("https://raw.githubusercontent.com/DevYukine/rom-converto/"
               "ea653069a79a643c588bd0ef11a67ca890990d70/resources/ps3_disc_keys.tsv")
MAX_CATALOG_BYTES = 256 * 1024


def download_disc_key(disc, check_cancel):
    """Fetch the same static resource for every disc. Keep its data in memory."""
    message = f"No verified disc key for {disc.title_id} in the online catalog. Use a decrypted copy of your disc."
    check_cancel()
    try:
        with urlopen(CATALOG_URL, timeout=10) as response:
            data = bytearray()
            while chunk := response.read(64 * 1024):
                check_cancel()
                data.extend(chunk)
                if len(data) > MAX_CATALOG_BYTES:
                    raise ValueError(message)
    except (OSError, HTTPException) as exc:
        check_cancel()
        raise ValueError("Could not reach the PS3 disc key catalog. Check your connection and resume, "
                         "or use a decrypted copy of your disc.") from exc
    check_cancel()
    title = disc.title_id.encode("ascii")
    for row in data.splitlines():
        serial, _, encoded = row.partition(b"\t")
        if serial == title and len(encoded) == 32:
            try:
                key = bytes.fromhex(encoded.decode("ascii"))
            except ValueError:
                continue
            if len(key) == 16 and hashlib.sha256(key).hexdigest() == disc.key_sha256:
                return key
    raise ValueError(message)
