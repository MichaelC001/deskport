#!/usr/bin/env python3
"""Adversarial tests for publication receipts and owned-run cleanup."""
import argparse
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("workflow", ROOT / "scripts/build-workflow.py")
w = importlib.util.module_from_spec(spec)
spec.loader.exec_module(w)
TEST_ROOT = Path(subprocess.check_output(["bash", ROOT / "scripts/build-paths.sh", "checkout"]).decode().strip()) / "tests"
TEST_ROOT.mkdir(parents=True, exist_ok=True)


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="workflow-", dir=TEST_ROOT)
        self.base = Path(self.temp.name)
        target = self.base / "runs.noindex/run/macos"
        self.paths = {key: target / key for key in ("work", "dist", "logs", "tests")}
        self.paths.update(base=self.base, target=target, cache=self.base / "cache", record=self.base / "records/run/macos")
        for key in ("work", "dist", "cache", "record"):
            self.paths[key].mkdir(parents=True)
        (self.paths["work"] / "object.o").write_bytes(b"object")
        (self.paths["cache"] / "library.a").write_bytes(b"retained library")
        self.package = self.paths["dist"] / "DeskPort-test.zip"
        self.package.write_bytes(b"package bytes")
        self.source = dict(revision="a" * 40, dirty=False, diff_sha256="b" * 64, core="c" * 40)
        self.record = dict(owner=w.OWNERSHIP, schema=w.SCHEMA, checkout=str(ROOT), run="run", target="macos",
                           source=self.source, source_unchanged=True, status="notarized", assets=[w.asset_info(self.paths, self.package)])
        w.freeze(self.paths, self.record)
        self.args = argparse.Namespace(run="run", target="macos", apply=True, work_only=False, repo="owner/repo", tag="v1")
        self.asset = dict(id=12, name=self.package.name, size=self.package.stat().st_size,
                          digest="sha256:" + w.digest(self.package), state="uploaded",
                          browser_download_url="https://github.com/owner/repo/releases/download/v1/DeskPort-test.zip")
        self.release = dict(id=1, draft=False, assets=[self.asset])

    def tearDown(self):
        self.temp.cleanup()

    def receipt(self):
        w.write_json(self.paths["record"] / "github-receipt.json", {"repo": "owner/repo", "tag": "v1"})

    def remote_patches(self):
        return patch.object(w, "release_metadata", return_value=self.release), patch.object(
            w, "remote_hash", return_value=(w.digest(self.package), self.package.stat().st_size))

    def test_path_query_creates_nothing_and_ignores_output_override(self):
        root = self.base / "fresh"
        with patch.dict(os.environ, {"DESKPORT_BUILD_ROOT": str(root), "DESKPORT_MACOS_DIST_DIR": str(ROOT)}):
            paths, env = w.layout("macos", "valid-id")
            self.assertFalse(root.exists())
            self.assertEqual(env["DESKPORT_MACOS_DIST_DIR"], str(paths["dist"]))
            self.assertIn("runs.noindex", str(paths["dist"]))

    def test_path_traversal_and_source_outputs_refused(self):
        for run in ("../escape", ".", "..", "", "/absolute"):
            with self.assertRaises(w.Refused):
                w.layout("macos", run)
        with patch.dict(os.environ, {"DESKPORT_BUILD_ROOT": str(ROOT / "build")}):
            with self.assertRaises((w.Refused, subprocess.CalledProcessError)):
                w.layout("macos", "run")

    def test_private_config_is_data_not_shell(self):
        config = self.base / "signing.env"
        config.write_text("DESKPORT_SIGN_" + "IDENTITY='$(touch BAD)'\n")
        config.chmod(0o600)
        with self.assertRaises(w.Refused):
            w.local_signing({"DESKPORT_BUILD_ROOT": str(self.base / "outputs")}, str(config))
        self.assertFalse((ROOT / "BAD").exists())
        config.write_text("DESKPORT_SIGN_" + "IDENTITY='literal identity'\n")
        env = {"DESKPORT_BUILD_ROOT": str(self.base / "outputs")}
        w.local_signing(env, str(config))
        self.assertEqual(env["DESKPORT_SIGN_IDENTITY"], "literal identity")
        config.chmod(0o644)
        with self.assertRaises(w.Refused):
            w.local_signing(env, str(config))

    def test_signing_config_cannot_hide_in_source_via_relative_path(self):
        relative = os.path.relpath(self.base / "signing.env", ROOT)
        (self.base / "signing.env").write_text("# private configuration\n")
        (self.base / "signing.env").chmod(0o600)
        with patch.object(w, "ROOT", self.base), self.assertRaises(w.Refused):
            w.local_signing({"DESKPORT_BUILD_ROOT": str(self.base / "outputs")}, relative)

    def test_cleanup_mount_root_is_refused(self):
        original = os.path.ismount
        with patch.object(os.path, "ismount", side_effect=lambda p: Path(p) == self.paths["work"] or original(p)):
            with self.assertRaises(w.Refused):
                w.deletion_guard(self.paths["work"], self.paths)

    def test_missing_upload_receipt_keeps_everything(self):
        with self.assertRaises(w.Refused):
            w.clean(self.args, self.paths, self.record)
        self.assertTrue(self.package.exists())
        self.assertTrue(self.paths["work"].exists())

    def test_remote_replacement_or_missing_asset_refused(self):
        self.receipt()
        self.asset["digest"] = "sha256:" + "0" * 64
        with patch.object(w, "release_metadata", return_value=self.release), patch.object(w, "remote_hash", return_value=("bad", 13)):
            with self.assertRaises(w.Refused):
                w.clean(self.args, self.paths, self.record)
        self.release["assets"] = []
        with patch.object(w, "release_metadata", return_value=self.release):
            with self.assertRaises(w.Refused):
                w.verify_remote(self.args, self.paths, self.record)
        self.assertTrue(self.package.exists())

    def test_draft_not_completed_delivery(self):
        self.release["draft"] = True
        with patch.object(w, "release_metadata", return_value=self.release), patch.object(w, "remote_hash") as download:
            with self.assertRaises(w.Refused):
                w.verify_remote(self.args, self.paths, self.record)
            download.assert_not_called()

    def test_changed_work_refused(self):
        self.args.work_only = True
        (self.paths["work"] / "user-notes.txt").write_text("retain")
        with self.assertRaises(w.Refused):
            w.clean(self.args, self.paths, self.record)
        self.assertTrue(self.paths["work"].exists())

    def test_protected_files_refused_even_after_freeze(self):
        for name in (".keep", ".git", "personal.p12"):
            item = self.paths["work"] / name
            item.write_text("preserve")
            w.freeze(self.paths, self.record)
            self.args.work_only = True
            with self.assertRaises(w.Refused):
                w.clean(self.args, self.paths, self.record)
            item.unlink()

    def test_allocation_only_change_after_freeze_allows_cleanup(self):
        self.args.work_only = True
        self.record["trees"]["work"]["bytes"] -= 4096
        with patch.object(w, "inactive"), patch.object(w, "git_state", return_value=self.source), redirect_stdout(io.StringIO()):
            w.clean(self.args, self.paths, self.record)
        self.assertFalse(self.paths["work"].exists())
        self.assertTrue(self.package.exists())
        self.assertTrue((self.paths["cache"] / "library.a").exists())

    def test_allocation_only_change_after_plan_allows_cleanup(self):
        self.args.work_only = True
        original = w.footprint
        calls = 0

        def allocation_settles(path):
            nonlocal calls
            current = original(path)
            if path == self.paths["work"]:
                calls += 1
                if calls > 1:
                    current["bytes"] += 4096
            return current

        with patch.object(w, "footprint", side_effect=allocation_settles), patch.object(w, "inactive"), patch.object(
                w, "git_state", return_value=self.source), redirect_stdout(io.StringIO()):
            w.clean(self.args, self.paths, self.record)
        self.assertGreater(calls, 1)
        self.assertFalse(self.paths["work"].exists())
        self.assertTrue(self.package.exists())

    def test_metadata_change_after_plan_still_refuses_cleanup(self):
        self.args.work_only = True
        calls = 0

        def change_work_during_recheck(path):
            nonlocal calls
            if path == self.paths["work"]:
                calls += 1
                if calls > 1:
                    (path / "user-notes.txt").write_text("retain")

        with patch.object(w, "inactive", side_effect=change_work_during_recheck), patch.object(
                w, "git_state", return_value=self.source), redirect_stdout(io.StringIO()), self.assertRaises(w.Refused):
            w.clean(self.args, self.paths, self.record)
        self.assertEqual((self.paths["work"] / "user-notes.txt").read_text(), "retain")
        self.assertTrue(self.package.exists())

    def test_inbound_reference_from_other_run_refused(self):
        (self.paths["cache"] / "borrowed").symlink_to(self.paths["work"])
        with self.assertRaises(w.Refused):
            w.deletion_guard(self.paths["work"], self.paths)

    def test_same_run_link_is_safe_only_when_both_are_planned(self):
        (self.paths["work"] / "output").symlink_to(self.paths["dist"])
        with self.assertRaises(w.Refused):
            w.deletion_guard(self.paths["dist"], self.paths)
        w.deletion_guard(self.paths["dist"], self.paths, [self.paths["work"], self.paths["dist"]])

    def test_work_only_deletes_owned_objects_and_preserves_cache_packages_source(self):
        (self.paths["work"] / "cache").symlink_to(self.paths["cache"])
        (self.paths["work"] / "scripts").symlink_to(ROOT / "scripts")
        w.freeze(self.paths, self.record)
        self.args.work_only = True
        with patch.object(w, "inactive"), patch.object(w, "git_state", return_value=self.source), redirect_stdout(io.StringIO()):
            w.clean(self.args, self.paths, self.record)
        self.assertFalse(self.paths["work"].exists())
        self.assertTrue(self.package.exists())
        self.assertEqual((self.paths["cache"] / "library.a").read_bytes(), b"retained library")
        self.assertTrue((ROOT / "scripts/build.sh").exists())

    def test_full_cleanup_rechecks_remote_and_leaves_only_cache_and_records(self):
        self.receipt()
        (self.paths["work"] / "output").symlink_to(self.paths["dist"])
        w.freeze(self.paths, self.record)
        a, b = self.remote_patches()
        with a, b as downloaded, patch.object(w, "inactive"), patch.object(w, "git_state", return_value=self.source), redirect_stdout(io.StringIO()):
            w.clean(self.args, self.paths, self.record)
            downloaded.assert_not_called()
        self.assertFalse(self.paths["target"].exists())
        self.assertTrue((self.paths["cache"] / "library.a").exists())
        self.assertTrue((self.paths["record"] / "github-receipt.json").exists())
        self.assertTrue((self.paths["record"] / "cleanup-journal.json").exists())

    def test_plan_does_not_delete(self):
        self.args.work_only, self.args.apply = True, False
        with patch.object(w, "inactive"), redirect_stdout(io.StringIO()):
            w.clean(self.args, self.paths, self.record)
        self.assertTrue(self.paths["work"].exists())

    def test_mixed_linux_package_names_are_collected(self):
        for name in ("deskport_1_amd64.deb", "deskport-1.x86_64.rpm", "deskport-1.pkg.tar.zst", "notary-private.log"):
            (self.paths["dist"] / name).write_bytes(b"package")
        names = {a["name"] for a in w.collect_assets(self.paths, "linux-portable", "built")}
        self.assertTrue({"deskport_1_amd64.deb", "deskport-1.x86_64.rpm", "deskport-1.pkg.tar.zst"} <= names)
        self.assertNotIn("notary-private.log", names)

    def test_source_materials_are_excluded_from_packages_and_uploads(self):
        for name in ("DeskPort-1-source.tar.gz", "DeskPort-1-linux-source.tar.gz", "source-materials-1-windows.tar.gz", "relink-materials-1-windows.tar.gz"):
            path = self.paths["dist"] / name
            path.write_bytes(b"not a release asset")
            self.assertTrue(w.source_material_archive(name))
        names = {a["name"] for a in w.collect_assets(self.paths, "windows", "built")}
        self.assertEqual(names, {self.package.name})
        self.record["assets"].append(w.asset_info(self.paths, self.paths["dist"] / "DeskPort-1-source.tar.gz"))
        with patch.object(w, "privacy"), patch.object(w.subprocess, "run") as upload:
            with self.assertRaises(w.Refused):
                w.publish(self.args, self.paths, self.record)
            upload.assert_not_called()

    def test_finishing_package_never_generates_source_or_relink_archives(self):
        self.args.target = "windows"
        self.record["status"] = "built"
        with patch.object(w, "git_state", return_value=self.source), patch.object(w, "run_logged") as command:
            w.finish_package(self.args, self.paths, {}, self.record)
            command.assert_not_called()
        self.assertEqual(len(self.record["assets"]), 3)
        self.assertFalse(any(w.source_material_archive(a["name"]) for a in self.record["assets"]))
        metadata = next(a for a in self.record["assets"] if a["name"].endswith("-build.json"))
        contents = json.loads((self.paths["dist"] / metadata["path"]).read_text())
        self.assertTrue(contents["source_url"].endswith(self.source["revision"]))

    def test_server_digest_verifies_without_downloading(self):
        with patch.object(w, "release_metadata", return_value=self.release), patch.object(w, "remote_hash") as download:
            receipt = w.verify_remote(self.args, self.paths, self.record)
            self.assertEqual(receipt["assets"][0]["verification"], "github-sha256")
            download.assert_not_called()

    def test_missing_or_invalid_digest_keeps_files_without_implicit_download(self):
        for value in (None, "", "sha256:bad", "md5:" + "a" * 32):
            self.asset["digest"] = value
            with patch.object(w, "release_metadata", return_value=self.release), patch.object(w, "remote_hash") as download:
                with self.assertRaises(w.Refused):
                    w.verify_remote(self.args, self.paths, self.record)
                download.assert_not_called()
            self.assertTrue(self.package.exists())

    def test_explicit_download_verification_for_legacy_asset(self):
        self.asset["digest"] = None
        self.args.download_verify = True
        a, b = self.remote_patches()
        with a, b as download:
            receipt = w.verify_remote(self.args, self.paths, self.record)
            self.assertEqual(receipt["assets"][0]["verification"], "download-sha256")
            download.assert_called_once()

    def test_refuses_symlink_asset_and_run(self):
        link = self.paths["dist"] / "DeskPort-secret.zip"
        link.symlink_to(self.base / "outside")
        with self.assertRaises(w.Refused):
            w.asset_info(self.paths, link)
        self.record["checkout"] = "/different/source"
        w.write_json(self.paths["record"] / "run.json", self.record)
        with self.assertRaises(w.Refused):
            w.read_record(self.paths, self.args)

    def test_lock_excludes_concurrent_run(self):
        with w.locked(self.paths, "macos"):
            with self.assertRaises(w.Refused):
                with w.locked(self.paths, "macos"):
                    pass

    def test_publish_validates_all_inputs_before_first_upload(self):
        self.record["assets"].append(dict(path="missing-package.zip", name="DeskPort-test-extra.zip", size=1, sha256="bad"))
        with patch.object(w, "privacy"), patch.object(w.subprocess, "run") as upload:
            with self.assertRaises(w.Refused):
                w.publish(self.args, self.paths, self.record)
            upload.assert_not_called()

    def test_cache_budget_blocks_build_without_touching_run(self):
        args = argparse.Namespace(min_free_gib=10**9, max_cache_gib=40)
        with self.assertRaises(w.Refused):
            w.capacity(self.paths, args)

    def fresh_build(self):
        paths = dict(self.paths)
        paths["target"] = self.base / "runs.noindex/fresh/linux-nix"
        paths["record"] = self.base / "records/fresh/linux-nix"
        paths.update({key: paths["target"] / key for key in ("work", "dist", "logs", "tests")})
        args = argparse.Namespace(command="build", target="linux-nix", run="fresh",
                                  min_free_gib=1, distribution=False, signing_config=None)
        return paths, args

    def test_invalid_jobs_leaves_no_unregistered_work(self):
        paths, args = self.fresh_build()
        with patch.object(w, "host_check"), patch.object(w, "tool_check"), \
                patch.object(w, "capacity"), patch.object(w, "privacy"), \
                patch.object(w, "git_state", return_value=self.source):
            with self.assertRaises(w.Refused):
                w.build(args, paths, {"DESKPORT_JOBS": "invalid"})
        self.assertFalse(paths["target"].exists())
        self.assertFalse(paths["record"].exists())

    def test_nix_result_link_is_disposable_work_and_smoke_tmp_is_removed(self):
        paths, args = self.fresh_build()
        store = self.base / "store-result"
        store.mkdir()
        (store / "application").write_bytes(b"retained Nix result")
        scratch = []

        def command(command, actual_paths, env, phase):
            self.assertEqual(actual_paths, paths)
            if phase == "build":
                self.assertEqual(command[-1], paths["work"] / "result")
                command[-1].symlink_to(store)
            elif phase == "smoke":
                self.assertEqual(command[-1], paths["work"] / "result")
                temporary = Path(env["TMPDIR"])
                self.assertEqual(temporary.parent, paths["base"].parent / "t")
                (temporary / "fixture").write_text("test data")
                scratch.append(temporary)

        with patch.object(w, "host_check"), patch.object(w, "tool_check"), \
                patch.object(w, "capacity"), patch.object(w, "privacy"), \
                patch.object(w, "git_state", return_value=self.source), \
                patch.object(w, "run_logged", side_effect=command), redirect_stdout(io.StringIO()):
            w.build(args, paths, {})
            self.assertEqual(len(scratch), 1)
            self.assertFalse(scratch[0].exists())
            self.assertFalse(paths["dist"].exists())
            args.work_only, args.apply = True, True
            record = w.read_record(paths, args)
            with patch.object(w, "inactive"):
                w.clean(args, paths, record)
        self.assertFalse(paths["target"].exists())
        self.assertEqual((store / "application").read_bytes(), b"retained Nix result")

    def test_process_level_github_upload_download_and_cleanup(self):
        """Exercise gh upload/server-digest protocol without package downloads."""
        fakebin = self.base / "fakebin"
        remote = self.base / "github"
        fakebin.mkdir()
        remote.mkdir()
        gh = fakebin / "gh"
        gh.write_text('''#!/usr/bin/env python3
import hashlib,json,os,pathlib,shutil,sys
root=pathlib.Path(os.environ['DESKPORT_TEST_GITHUB'])
args=sys.argv[1:]
files=sorted(root.iterdir())
if args[:2]==['release','upload']:
    source=pathlib.Path(args[3]); shutil.copyfile(source,root/source.name)
elif args[0]=='api' and '/releases/tags/' in args[1]:
    print(json.dumps({'id':1,'draft':False,'assets':[
      {'id':i+1,'name':p.name,'size':p.stat().st_size,'state':'uploaded',
       'digest':'sha256:'+hashlib.sha256(p.read_bytes()).hexdigest(),
       'browser_download_url':'https://example.invalid/'+p.name}
      for i,p in enumerate(files)]}))
elif args[0]=='api' and '/releases/assets/' in args[1]:
    sys.exit(19)  # Default verification must never download installer bytes.
else: sys.exit(9)
''')
        gh.chmod(0o755)
        metadata = self.paths["dist"] / "DeskPort-test-build.json"
        metadata.write_text(json.dumps({"source": self.source}))
        self.record["assets"].append(w.asset_info(self.paths, metadata))
        w.freeze(self.paths, self.record)
        with patch.dict(os.environ, {"PATH": str(fakebin) + os.pathsep + os.environ["PATH"], "DESKPORT_TEST_GITHUB": str(remote)}), \
                patch.object(w, "privacy"), patch.object(w, "git_state", return_value=self.source), \
                patch.object(w, "inactive"), redirect_stdout(io.StringIO()):
            w.publish(self.args, self.paths, self.record)
            receipt = json.loads((self.paths["record"] / "github-receipt.json").read_text())
            self.assertEqual(len(receipt["assets"]), 2)
            w.clean(self.args, self.paths, self.record)
        self.assertFalse(self.paths["target"].exists())
        self.assertEqual(len(list(remote.iterdir())), 2)
        self.assertTrue((self.paths["cache"] / "library.a").exists())


if __name__ == "__main__":
    unittest.main()
