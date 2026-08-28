using System.Collections.Generic;
using UnityEngine;

namespace Endfield.NativeInterop
{
    // Manages synchronization between Unity Scene and our Custom C++ ECS
    // (Bypassing Unity DOTS)
    public class CustomECSManager : MonoBehaviour
    {
        private void Start()
        {
            // Initialize native ECS
        }

        private void Update()
        {
            // Gather transforms and data of "dirty" objects and send to Native ECS
            // This avoids managed overhead during the render loop itself
        }
    }
}

