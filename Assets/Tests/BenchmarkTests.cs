using System.Collections.Generic;
using System.IO;
using NUnit.Framework;
using UnityEngine;
using Endfield.NativeInterop;

namespace Endfield.Tests
{
    public class BenchmarkTests
    {
        [Test]
        public void NativeBenchmarkStats_CullingRatio_CalculatesCorrectly()
        {
            var stats = new NativePluginWrapper.NativeBenchmarkStats
            {
                totalInstances = 1000,
                visibleInstances = 200,
                culledFrustum = 800
            };

            // 1000 total, 200 visible -> 80% culled
            Assert.AreEqual(80.0f, stats.CullingRatio, 0.01f);
        }

        [Test]
        public void NativeBenchmarkStats_ZeroTotalInstances_CullingRatioIsZero()
        {
            var stats = new NativePluginWrapper.NativeBenchmarkStats
            {
                totalInstances = 0,
                visibleInstances = 0
            };

            Assert.AreEqual(0.0f, stats.CullingRatio);
        }

        [Test]
        public void BenchmarkReportGenerator_GeneratesValidMarkdown()
        {
            var results = new List<BenchmarkTierResult>
            {
                new BenchmarkTierResult
                {
                    tierName = "10K",
                    entityCount = 10000,
                    unityFps = 120.0f,
                    unityFrameTimeMs = 8.33f,
                    nativeStats = new NativePluginWrapper.NativeBenchmarkStats
                    {
                        totalInstances = 10000,
                        visibleInstances = 2500,
                        culledFrustum = 7500,
                        ecsQueryTimeMs = 0.12f,
                        frustumCullingTimeMs = 0.45f,
                        sortingTimeMs = 0.35f,
                        batchingTimeMs = 0.25f,
                        totalNativeFrameTimeMs = 1.17f
                    }
                }
            };

            string md = BenchmarkReportGenerator.GenerateMarkdownReport("Unit Test Run", results, "Test Rig");

            Assert.IsTrue(md.Contains("Arknights: Endfield"));
            Assert.IsTrue(md.Contains("10K"));
            Assert.IsTrue(md.Contains("10,000"));
            Assert.IsTrue(md.Contains("2,500"));
            Assert.IsTrue(md.Contains("75.0%"));
            Assert.IsTrue(md.Contains("1.170 ms"));
        }

        [Test]
        public void BenchmarkReportGenerator_SavesAndLoadsFile()
        {
            string testContent = "# Sample Test Report Content";
            string testFileName = "UnitTestReport_Sample.md";

            string savedPath = BenchmarkReportGenerator.SaveReportToFile(testContent, testFileName);
            Assert.IsNotNull(savedPath);
            Assert.IsTrue(File.Exists(savedPath));

            string readBack = File.ReadAllText(savedPath);
            Assert.AreEqual(testContent, readBack);

            // Cleanup
            if (File.Exists(savedPath))
            {
                File.Delete(savedPath);
            }
        }
    }
}

