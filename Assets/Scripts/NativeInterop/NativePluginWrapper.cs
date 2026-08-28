using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace Endfield.NativeInterop
{
    public static class NativePluginWrapper
    {
        private const string pluginName = "NativeCore";

        [DllImport(pluginName)]
        public static extern void ExecuteNativeRenderLoop(int eventID);

        [DllImport(pluginName)]
        public static extern void RegisterEntity(uint id, IntPtr transformData);

        // Helper to get the native rendering event pointer for CommandBuffer.IssuePluginEvent
        [DllImport(pluginName)]
        public static extern IntPtr GetRenderEventFunc();
    }
}

