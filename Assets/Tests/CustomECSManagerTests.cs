using System.Collections;
using System.Collections.Generic;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;
using Endfield.NativeInterop;

namespace Endfield.Tests
{
    public class CustomECSManagerTests
    {
        [Test]
        public void CustomECSManager_CanBeAttachedToGameObject()
        {
            // Arrange
            var go = new GameObject("TestECSManager");

            // Act
            var ecsManager = go.AddComponent<CustomECSManager>();

            // Assert
            Assert.IsNotNull(ecsManager, "CustomECSManager should be successfully attached to a GameObject.");

            // Cleanup
            Object.DestroyImmediate(go);
        }

        [Test]
        public void NativeRenderFeature_Initialization_DoesNotThrow()
        {
            // Act & Assert
            // (We can't easily run the full ScriptableRendererFeature in an EditMode test, 
            // but we can ensure its constructor or simple properties don't crash)
            Assert.DoesNotThrow(() =>
            {
                var feature = ScriptableObject.CreateInstance<NativeRenderFeature>();
                Assert.IsNotNull(feature);
                Object.DestroyImmediate(feature);
            });
        }
    }
}

