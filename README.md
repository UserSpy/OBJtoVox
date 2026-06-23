# OBJtoVox
Voxelizes .obj models with textures into colored .vox files

This was built using AI. I have no idea how long this would have taken me if I did it fully myself. This is free and only took a couple days of work.
My original goal with this was so I could import 3d models into Teardown. However I'm also working on a Vox to Minecraft Schematic/Litematic converter and plan to eventually release an all in one.

## Usage


<details>
  <summary>Setting up the OBJ</summary>
  Currently this tool has only been tested to use Base Color and Alpha on Principled BSDF in Blender to decide color/opacity. Before using this tool, make sure your OBJ has your desired color or image set to the base color for all visible materials and optionally either the alpha map from a PNG or an opacity map plugged into the alpha, though make sure that if using an opacity map it doesn't have an alpha channel with any transparency. A good rule of thumb is that if the model doesn't look correct in Blender using only the base color, it is not going to look correct after being voxelized. In the image below the model still looks good even after disconnecting the specular texture, so this should be good to go.                          

  ### Example: 
  
  <img align="center" width="759" height="647" alt="image" src="https://github.com/user-attachments/assets/9fc94258-f353-47b7-90c7-d5c81aaff234" />

  
 
 
 
 Also make sure that rotation and scale are correct and have been applied with CTRL + A. 
  
  <img width="259" height="425" alt="image" src="https://github.com/user-attachments/assets/bdefa567-bc94-4efa-a57b-c3b74067079d" />
  
  If any changes have been made, export the OBJ to your desired folder. I reccomend using Path Mode: Copy.
  
  <img width="227" height="94" alt="image" src="https://github.com/user-attachments/assets/74dce8c8-d42b-4b45-a48e-69c1402629dd" />
</details>



  Drop OBJtoVoxX.X.exe into the model folder, and search CMD in file explorer's search bar and press enter
  
  <img width="695" height="333" alt="image" src="https://github.com/user-attachments/assets/5fcc5dc8-cd49-4291-bd8e-197bedaf55d6" />
  
  In the CMD window, run OBJtoVoxX.X.exe modelname.obj
  
  <img width="513" height="71" alt="image" src="https://github.com/user-attachments/assets/f4dcaec1-8987-47c7-833c-cf6eca9ddee1" />
  
  This should create a file named "modelname_0_12.vox". Open this in MagicaVoxel to confirm it looks correct. If the model is missing textures, this could be because the texture is no longer where the MTL file expects it to be. If texture files use special characters like chinese characters, may not work either.
  
  <img width="1194" height="624" alt="image" src="https://github.com/user-attachments/assets/0f030410-e9c6-48d6-afd9-c9e396ed5055" />
  
  There should now also be a config.txt present in the folder. Click it to open the config
  
  <img width="209" height="172" alt="image" src="https://github.com/user-attachments/assets/7776619d-ae9b-4ceb-b053-1a88c036c0bf" />
  
  You can then edit the values
  
  <img width="220" height="155" alt="image" src="https://github.com/user-attachments/assets/21abc371-b286-48d8-9b61-02866bd85a8f" />

  
  Edit and save the config file, then run the command again to voxelize with the different settings.
  Explanations for each variable are in the config file, but will give examples of what they do here: 
  
  ### scale: Attempts to multiply the size of the model by this value
  
  ### scale=10.0:
  <img width="1198" height="625" alt="image" src="https://github.com/user-attachments/assets/96e21fa0-b45d-45a4-bcf7-884330c07c84" />
  
  ### chunksize: Default 128. Anything greater than 256 will cause integer overflow and corrupt the .vox. Smaller chunks use less RAM during conversion but may take more time and hard drive space
  
  ### chunksize=10: <img width="469" height="290" alt="image" src="https://github.com/user-attachments/assets/ab7361fe-b657-48c1-9889-414c1c3db357" />
  
  ### chunksize=300: <img width="517" height="210" alt="image" src="https://github.com/user-attachments/assets/3a0bc8c0-b9db-43fb-9648-6a61cd9cefa6" />
  
  
  
  ### maxDim: Maximum dimension size, automatically lowers scale if scale is too big. MagicaVoxel squishes/poorly handles .vox files with dimensions greater than 2000x2000x2000 voxels, but other software (Like Teardown) can handle large dimensions well.
  
  ### maxDim=3000 (scale=50): <img width="381" height="244" alt="image" src="https://github.com/user-attachments/assets/e1c234f8-55f0-4817-bcf6-65854ab1e694" />
  <img width="1533" height="863" alt="image" src="https://github.com/user-attachments/assets/41250155-72e8-460b-8dab-1fea7484b044" />


  
  
  ### onFloor: When true, the .vox model will be offset so that the lowest part is at Z = 0. Keep in mind this will cut MagicaVoxel max height in half, but can make using/viewing the model easier

  ### onFloor=false: <img width="497" height="263" alt="image" src="https://github.com/user-attachments/assets/53a87dc1-01c8-4491-a124-00d837eb5fb4" />

  
  ### colorRanges: Determines which indexes should be used for colors. Most use cases should just leave at 1-255, but Teardown determines materials based on color index and only uses 1-184. Check Teardown modding wiki for palette details.
  
  ### colorRanges=1-10,20-30: <img width="835" height="311" alt="image" src="https://github.com/user-attachments/assets/dad31c4a-e2fd-492d-9cb5-02c9d0f3080d" />
  
  
  ### voxelsPerUnit: Represents how many voxels is equal to 1 OBJ unit in length (1 OBJ unit = 1 meter in Blender). Is multiplied by scale to determine the final size.  A value of 12 is roughly 1m in Teardown, 1 is 1m in minecraft (personally think 0.914 might scale better for MC)
  
  ### voxelsPerUnit=0.914 (scale=10.0): <img width="459" height="289" alt="image" src="https://github.com/user-attachments/assets/a89786c3-dac6-4eb8-8188-1b4d80a63b12" />
  
  
  ### isSolidFill: Experimental and very slow for large dimensions, be patient. If false, only creates the surface/shell and leaves the inside empty. If true, fills in most empty voxels that are behind faces and not directly exposed. Best to test this on smaller scales first. If what are supposed to be outer surfaces get covered in a cloud of voxels, you have flipped normals/exposed backfaces. If the inside has a lot of holes, you may have a leak or some intersecting faces causing it.
  
  ### isSolidFill=false: <img width="567" height="419" alt="image" src="https://github.com/user-attachments/assets/04c74875-09e3-46cf-988c-6447c01db38f" />
  
  
  ### isSolidFill=true: <img width="480" height="383" alt="image" src="https://github.com/user-attachments/assets/8d5695b9-5d83-42f7-a5cf-bf24c9f8ba79" />
