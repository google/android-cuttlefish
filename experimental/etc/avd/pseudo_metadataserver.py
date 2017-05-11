#!/usr/bin/python
"""Pseudo metadata server for Cloud Android."""

import BaseHTTPServer
import datetime
import hashlib
import json
import os
import re
import SimpleHTTPServer
import SocketServer
import sys
import time
import urlparse

SSH_KEY_PATH = ["project", "attributes", "sshKeys"]


def ConvertToCamelCase(element):
  return re.sub(r"-[a-z]+", lambda m: m.group(0)[1:].capitalize(), element)


def ConvertFromCamelCase(element):
  if element == "sshKeys":
    return "sshKeys"
  return re.sub(r"[A-Z]", lambda m: "-" + m.group(0).lower(), element)


def SetValueForAttr(path, value, dest):
  it = dest
  for p in path[:-1]:
    key = ConvertToCamelCase(p)
    if key not in it:
      it[key] = {}
    it = it[key]
  it[path[-1]] = value


def GetValueForPath(path, values):
  """Find the first value that matches attr in the metadata file.

  Args:
    path: Array representing the split path to the attribute
    values: The nested dictionary of values
  Returns:
    The string value or None if not found.
  """
  rval = values
  for p in path:
    key = ConvertToCamelCase(p)
    if key not in rval:
      return None
    rval = rval[p]
  return rval


def GenerateTextValues(values, prefix=""):
  """Converts a nested dictionary to text format.

  Args:
    values: The object to convert
    prefix: A string that is added to each key for the recursive descent
  Returns:
    A string representing the text format
  """
  rval = ""
  if isinstance(values, list):
    for key in range(len(values)):
      rval += GenerateTextValues(values[key], "%s/%d" % (prefix, key))
  elif isinstance(values, dict):
    for key in values:
      rval += GenerateTextValues(values[key], "%s/%s" % (
          prefix, ConvertFromCamelCase(key)))
  else:
    rval = "%s %s\n" % (prefix[1:], str(values))
  return rval


class MetadataServer(SocketServer.ThreadingMixIn, BaseHTTPServer.HTTPServer):
  """Metadata HTTP server class."""

  def __init__(self, metadata_path, addr, HandlerClass):
    self.metadata_path = metadata_path
    self.etag = ""
    self.daemon_threads = True
    BaseHTTPServer.HTTPServer.__init__(self, addr, HandlerClass)

  def SetSshKeys(self, dest):
    """Sets ssh keys in a dictionary.

    Args:
      dest: The dictionary to modify
    """
    ssh_keys = ""

    if len(os.environ["SUDO_USER"]):
      key_directory = os.path.expanduser("~" + os.environ["SUDO_USER"])
    else:
      key_directory = os.path.expanduser("~")
    key_directory = os.path.join(key_directory, ".ssh")

    for filename in os.listdir(key_directory):
      if filename.endswith(".pub"):
        with open(os.path.join(key_directory, filename), "rb") as kf:
          kparts = kf.readline().rstrip().split(" ")
          ssh_keys += "%s:%s %s google-ssh %s@google.com\n" % (
              os.environ["USER"], kparts[0], kparts[1], os.environ["USER"])
    SetValueForAttr(SSH_KEY_PATH, ssh_keys, dest)

  def PollMetadataValues(self, etag, timeout_sec):
    """Gets the current state of the metadata values.

    A call to this function will block until said state is different than last
    time it was retrieved (as specified by the etag parameter) or timeout_sec
    seconds have passed.

    Args:
      etag: The ETag of the last version of the metadata obtained by the
          client.
      timeout_sec: The time the client is willing to wait for the metadata
          to change.
    Returns:
       The current state of the metadata values.
    """
    start_time = datetime.datetime.now()
    while True:
      (new_etag, values) = self.GetMetadataValues()
      time_diff = datetime.datetime.now() - start_time
      if etag == new_etag and timeout_sec > time_diff.total_seconds():
        time.sleep(1)
      else:
        return (new_etag, values)

  def GetMetadataValues(self):
    """Gets the current state of the metadata values."""
    with open(self.metadata_path, "rb") as f:
      rval = json.load(f)
    self.SetSshKeys(rval)
    etag = hashlib.md5(json.dumps(rval)).hexdigest()
    return (etag, rval)


class MetadataRequestHandler(SimpleHTTPServer.SimpleHTTPRequestHandler):
  """HTTP server request handler."""

  protocol_version = "HTTP/1.1"
  wbufsize = -1  # Turn off output buffering.

  def version_string(self):
    return "Metadata Server for VM"

  def do_GET(self):  # WSGI name, so pylint: disable=g-bad-name
    """Process a GET request."""
    parts = urlparse.urlparse(self.path)
    params = urlparse.parse_qs(parts.query)
    timeout_sec = None
    alt = params.get("alt", ["json"])[0]
    last_etag = params.get("last_etag", [""])[0]

    if params.get("wait_for_change", ["false"])[0] == "true":
      timeout_sec = float(params.get("timeout_sec", ["30"])[0])
      (etag, values) = self.server.PollMetadataValues(last_etag, timeout_sec)
    else:
      (etag, values) = self.server.GetMetadataValues()
    value = None
    if params.get("recursive", ["false"])[0] == "true":
      if alt == "json":
        value = json.dumps(values)
      else:
        value = GenerateTextValues(values)
    else:
      value = GetValueForPath(parts.path.split("/")[3:], values)
      if alt == "json":
        value = json.dumps(value)
    if value:
      self.send_response(200)
      self.send_header("Metadata-Flavor", "Google")
      self.send_header("Content-Type", "application/%s" % alt)
      self.send_header("Content-Length", len(value))
      self.send_header("ETag", etag)
      self.send_header("X-XSS-Protection", "1; mode=block")
      self.send_header("X-Frame-Options", "SAMEORIGIN")
      self.send_header("Connection", "close")
      self.end_headers()
      self.wfile.write(value)
    else:
      self.send_error(404, "Attribute not found %s" % parts.path)


def main():
  server_address = (sys.argv[1], int(sys.argv[2]))
  httpd = MetadataServer(sys.argv[3], server_address, MetadataRequestHandler)
  print "File %s" % sys.argv[3]
  print "Serving on ", httpd.socket.getsockname()
  httpd.serve_forever()


if __name__ == "__main__":
  main()
