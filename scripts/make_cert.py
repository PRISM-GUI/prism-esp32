"""Makes the crdt firmware's self-signed certificate: make_cert.py <cert.pem> <key.pem> <ip>.

With `cryptography`, which ESP-IDF's Python environment already has (its signing tools need it),
so the build needs no openssl. An EC P-256 key, valid ten years for the board's own address.
"""

import datetime
import ipaddress
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID


def certificate(key, ip):
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "PRISM board")])
    now = datetime.datetime.now(datetime.timezone.utc)
    return (
        x509.CertificateBuilder()
        .subject_name(name)
        .issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(days=1))
        .not_valid_after(now + datetime.timedelta(days=3650))
        .add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address(ip))]), critical=False)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .sign(key, hashes.SHA256())
    )


def main(cert_path, key_path, ip):
    key = ec.generate_private_key(ec.SECP256R1())
    with open(key_path, "wb") as out:
        out.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                    serialization.NoEncryption()))
    with open(cert_path, "wb") as out:
        out.write(certificate(key, ip).public_bytes(serialization.Encoding.PEM))


if __name__ == "__main__":
    main(*sys.argv[1:4])
