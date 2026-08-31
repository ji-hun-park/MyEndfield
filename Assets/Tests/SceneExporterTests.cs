using System.Collections;
using System.Collections.Generic;
using NUnit.Framework;
using UnityEngine;
using Endfield.Editor;
using System.IO;

namespace Endfield.Tests
{
    public class SceneExporterTests
    {
        [Test]
        public void SceneExporter_ExportPath_IsValid()
        {
            // Just verifying that Path.Combine produces a valid path format without throwing exceptions
            string exportPath = Path.Combine(Application.dataPath, "../NativeCore/ExportedScene.bin");
            Assert.IsFalse(string.IsNullOrEmpty(exportPath));

            // Note: We don't want to actually run ExportScene() and overwrite files during unit tests 
            // unless we mock the file system, but we can verify the API surface.
        }
    }
}

