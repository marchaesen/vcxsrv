import argparse
import enum
import glob
import hashlib
import os
import requests
import sys
import tempfile
import time
import yaml
import shutil

from pathlib import Path
from PIL import Image
from urllib import parse

import dump_trace_images

TRACES_DB_PATH = os.getcwd() + "/traces-db/"
RESULTS_PATH = os.getcwd() + "/results/"

def replay(trace_path, device_name):
    success = dump_trace_images.dump_from_trace(trace_path, [], device_name)

    if not success:
        print("[check_image] Trace %s couldn't be replayed. See above logs for more information." % (str(trace_path)))
        return None, None, None
    else:
        base_path = trace_path.parent
        file_name = trace_path.name
        files = glob.glob(str(base_path / "test" / device_name / (file_name + "-*" + ".png")))
        assert(files)
        image_file = files[0]
        files = glob.glob(str(base_path / "test" / device_name / (file_name + ".log")))
        assert(files)
        log_file = files[0]
        return hashlib.md5(Image.open(image_file).tobytes()).hexdigest(), image_file, log_file

def gitlab_download_metadata(project_url, repo_commit, trace_path):
    url = parse.urlparse(project_url)

    url_path = url.path
    if url_path.startswith("/"):
        url_path = url_path[1:]

    gitlab_api_url = url.scheme + "://" + url.netloc + "/api/v4/projects/" + parse.quote_plus(url_path)

    r = requests.get(gitlab_api_url + "/repository/files/%s/raw?ref=%s" % (parse.quote_plus(trace_path), repo_commit))
    metadata_raw = r.text.strip().split('\n')
    metadata = dict(line.split(' ', 1) for line in metadata_raw[1:])
    oid = metadata["oid"][7:] if metadata["oid"].startswith('sha256:') else metadata["oid"]
    size = int(metadata['size'])

    return oid, size

def gitlfs_download_trace(repo_url, repo_commit, trace_path, oid, size):
    headers = {
        "Accept": "application/vnd.git-lfs+json",
        "Content-Type": "application/vnd.git-lfs+json"
    }
    json = {
        "operation": "download",
        "transfers": [ "basic" ],
        "ref": { "name": "refs/heads/%s" % repo_commit },
        "objects": [
            {
                "oid": oid,
                "size": size
            }
        ]
    }

    r = requests.post(repo_url + "/info/lfs/objects/batch", headers=headers, json=json)
    url = r.json()["objects"][0]["actions"]["download"]["href"]
    open(TRACES_DB_PATH + trace_path, "wb").write(requests.get(url).content)

def checksum(filename, hash_factory=hashlib.sha256, chunk_num_blocks=128):
    h = hash_factory()
    with open(filename,'rb') as f:
        for chunk in iter(lambda: f.read(chunk_num_blocks*h.block_size), b''):
            h.update(chunk)
    return h.hexdigest()

def gitlab_ensure_trace(project_url, repo_commit, trace):
    trace_path = TRACES_DB_PATH + trace['path']
    if project_url is None:
        assert(repo_commit is None)
        assert(os.path.exists(trace_path))
        return

    os.makedirs(os.path.dirname(trace_path), exist_ok=True)

    if os.path.exists(trace_path):
        local_oid = checksum(trace_path)

    remote_oid, size = gitlab_download_metadata(project_url, repo_commit, trace['path'])

    if not os.path.exists(trace_path) or local_oid != remote_oid:
        print("[check_image] Downloading trace %s" % (trace['path']), end=" ", flush=True)
        download_time = time.time()
        gitlfs_download_trace(project_url + ".git", repo_commit, trace['path'], remote_oid, size)
        print("took %ds." % (time.time() - download_time), flush=True)

def gitlab_check_trace(project_url, repo_commit, device_name, trace, expectation):
    gitlab_ensure_trace(project_url, repo_commit, trace)

    result = {}
    result[trace['path']] = {}

    trace_path = Path(TRACES_DB_PATH + trace['path'])
    checksum, image_file, log_file = replay(trace_path, device_name)
    if checksum is None:
        return False
    elif checksum == expectation['checksum']:
        print("[check_image] Images match for %s" % (trace['path']))
        ok = True
    else:
        print("[check_image] Images differ for %s (expected: %s, actual: %s)" %
                (trace['path'], expectation['checksum'], checksum))
        print("[check_image] For more information see "
                "https://gitlab.freedesktop.org/mesa/mesa/blob/master/.gitlab-ci/tracie/README.md")
        ok = False

    trace_dir = os.path.split(trace['path'])[0]
    dir_in_results = os.path.join(trace_dir, "test", device_name)
    results_path = os.path.join(RESULTS_PATH, dir_in_results)
    os.makedirs(results_path, exist_ok=True)
    shutil.move(log_file, os.path.join(results_path, os.path.split(log_file)[1]))
    if not ok or os.environ.get('TRACIE_STORE_IMAGES', '0') == '1':
        image_name = os.path.split(image_file)[1]
        shutil.move(image_file, os.path.join(results_path, image_name))
        result[trace['path']]['image'] = os.path.join(dir_in_results, image_name)

    result[trace['path']]['expected'] = expectation['checksum']
    result[trace['path']]['actual'] = checksum

    return ok, result

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--file', required=True,
                        help='the name of the traces.yml file listing traces and their checksums for each device')
    parser.add_argument('--device-name', required=True,
                        help="the name of the graphics device used to replay traces")

    args = parser.parse_args()

    with open(args.file, 'r') as f:
        y = yaml.safe_load(f)

    if "traces-db" in y:
        project_url = y["traces-db"]["gitlab-project-url"]
        commit_id = y["traces-db"]["commit"]
    else:
        project_url = None
        commit_id = None

    traces = y['traces']
    all_ok = True
    results = {}
    for trace in traces:
        for expectation in trace['expectations']:
            if expectation['device'] == args.device_name:
                ok, result = gitlab_check_trace(project_url, commit_id, args.device_name, trace, expectation)
                all_ok = all_ok and ok
                results.update(result)

    with open(os.path.join(RESULTS_PATH, 'results.yml'), 'w') as f:
        yaml.safe_dump(results, f, default_flow_style=False)


    sys.exit(0 if all_ok else 1)

if __name__ == "__main__":
    main()
