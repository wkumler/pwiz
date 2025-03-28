﻿/*
 * Original author: Vagisha Sharma <vsharma .at. u.washington.edu>,
 *                  MacCoss Lab, Department of Genome Sciences, UW
 *
 * Copyright 2013 University of Washington - Seattle, WA
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

using System.Windows.Forms;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Newtonsoft.Json.Linq;
using pwiz.Skyline.FileUI;
using pwiz.SkylineTestUtil;

namespace pwiz.SkylineTest
{
    [TestClass]
    public class PublishDocAddSubfolderTest : AbstractUnitTest
    {
        [TestMethod]
        public void TestAddSubfolder()
        {
            // Test for JSON containing no writable folders. Tree should be empty.
            JToken containers = JObject.Parse(NOWRITABLEFOLDER_JSON);
            TreeNode rootNode = new TreeNode("Root");
            PublishDocumentDlg.AddChildContainers(null, rootNode, containers);
            Assert.AreEqual(0, rootNode.Nodes.Count);


            // Test for JSON containing 1 writable subfolder. 
            containers = JObject.Parse(WRITABLEFOLDER_1_JSON);
            rootNode = new TreeNode("Root");
            PublishDocumentDlg.AddChildContainers(null, rootNode, containers);
            Assert.AreEqual(1, rootNode.Nodes.Count);
            TreeNode maccossFolder = rootNode.Nodes[0];
            Assert.AreEqual("MacCoss", maccossFolder.Text); // Readable
            Assert.AreEqual(1, maccossFolder.Nodes.Count);
            Assert.AreEqual("user1", maccossFolder.Nodes[0].Text); // Writable sub-folder


            // Test for JSON containing 
            // 1. a writable folder where the user has no permissions on the parent folder.
            // 2. a writable folder that does not have the TargetedMS module enabled.
            //    All 3 top-level folders (Carr, MacCoss, Gibson) have writable sub-folders but
            //    only the MacCoss and Gibson folders have TargetedMS enabled sub-folders.
            containers = JObject.Parse(WRITABLEFOLDER_2_JSON);
            rootNode = new TreeNode("Root");
            PublishDocumentDlg.AddChildContainers(null, rootNode, containers);
            Assert.AreEqual(2, rootNode.Nodes.Count);
            maccossFolder = rootNode.Nodes[0];
            Assert.AreEqual("MacCoss", maccossFolder.Text); // No permissions
            Assert.AreEqual(1, maccossFolder.Nodes.Count);
            Assert.AreEqual("user1", maccossFolder.Nodes[0].Text); // Writable sub-folder

            TreeNode gibsonFolder = rootNode.Nodes[1];
            Assert.AreEqual("Gibson", gibsonFolder.Text); // Readable 
            Assert.AreEqual(1, gibsonFolder.Nodes.Count);
            Assert.AreEqual("userFolder", gibsonFolder.Nodes[0].Text); // Writable sub-folder

        }


        private const string NOWRITABLEFOLDER_JSON = "{sortOrder: 0, children: [\n" +
                                                     "    {\n" +
                                                     "     sortOrder: 0, \n" +
                                                     "     children:[\n" +
                                                     "         {\n" +
                                                     "         sortOrder: 0, \n" +
                                                     "         children:[ ], \n" +
                                                     "         folderType: \"Targeted MS\", \n" +
                                                     "         type: \"folder\", \n" +
                                                     "         activeModules: [\"Core\", \"Wiki\", \"TargetedMS\"], \n" +
                                                     "         effectivePermissions: [ \n" +  // READABLE
                                                     "             \"org.labkey.api.security.permissions.ReadPermission\" \n" +
                                                     "         ], \n" +
                                                     "         name: \"public\", \n" +
                                                     "         path: \"/Gibson/public\"\n" +
                                                     "         }\n" +
                                                     "     ], \n" +
                                                     "     folderType: \"Collaboration\", \n" +
                                                     "     type: \"project\", \n" +
                                                     "     activeModules: [\"Core\", \"Wiki\"], \n" +
                                                     "     effectivePermissions: [  \n" +  // READABLE
                                                     "         \"org.labkey.api.security.permissions.ReadPermission\" \n" +
                                                     "     ], \n" +
                                                     "     name: \"Gibson\", \n" +
                                                     "     path: \"/Gibson\"\n" +
                                                     "     }, \n" +
                                                     "    {\n" +
                                                     "     sortOrder: 0, \n" +
                                                     "     children:[\n" +
                                                     "         {\n" +
                                                     "         sortOrder: 0, \n" +
                                                     "         children:[ ], \n" +
                                                     "         folderType: \"Targeted MS\", \n" +
                                                     "         type: \"folder\", \n" +
                                                     "         activeModules: [\"Core\", \"Wiki\", \"TargetedMS\"], \n" +
                                                     "         effectivePermissions: [ \n" +  // READABLE
                                                     "             \"org.labkey.api.security.permissions.ReadPermission\" \n" +
                                                     "         ], \n" +
                                                     "         name: \"user1\", \n" +
                                                     "         path: \"/MacCoss/user1\"\n" +
                                                     "         }\n" +
                                                     "     ], \n" +
                                                     "     folderType: \"Collaboration\", \n" +
                                                     "     type: \"project\", \n" +
                                                     "     activeModules: [\"Core\", \"Wiki\"], \n" +
                                                     "     effectivePermissions: [ ], \n" + // NO PERMISSIONS
                                                     "     name: \"MacCoss\", \n" +
                                                     "     path: \"/MacCoss\"\n" +
                                                     "     } \n" +
                                                     "], \n" +
                                                     "folderType: \"None\", \n" +
                                                     "type: \"folder\", \n" +
                                                     "activeModules: [ ], \n" +
                                                     "title: \"\", \n" +
                                                     "effectivePermissions: [ ], \n" +
                                                     "name: \"\", \n" +
                                                     "path: \"/\", \n" +
                                                     "parentPath: \"null\", \n" +
                                                     "effectivePermissions: [ ]\n" +
                                                     "}";

        private const string WRITABLEFOLDER_1_JSON = "{sortOrder: 0, children: [\n" +
                                                        "    {\n" +
                                                        "     sortOrder: 0, \n" +
                                                        "     children:[\n" +
                                                        "         {\n" +
                                                        "         sortOrder: 0, \n" +
                                                        "         children:[ ], \n" +
                                                        "         folderType: \"Targeted MS\", \n" +
                                                        "         type: \"folder\", \n" +
                                                        "         activeModules: [\"Core\", \"Wiki\", \"TargetedMS\"], \n" +
                                                        "         effectivePermissions: [ \n" +  // READABLE
                                                        "             \"org.labkey.api.security.permissions.ReadPermission\" \n" +
                                                        "         ], \n" +
                                                        "         name: \"public\", \n" +
                                                        "         path: \"/Gibson/public\"\n" +
                                                        "         }\n" +
                                                        "     ], \n" +
                                                        "     folderType: \"Collaboration\", \n" +
                                                        "     type: \"project\", \n" +
                                                        "     activeModules: [\"Core\", \"Wiki\"], \n" +
                                                        "     effectivePermissions: [ ],\n" + // NO PERMISSIONS
                                                        "     name: \"Gibson\", \n" +
                                                        "     path: \"/Gibson\"\n" +
                                                        "     }, \n" +
                                                        "    {\n" +
                                                        "     sortOrder: 0, \n" +
                                                        "     children:[\n" +
                                                        "         {\n" +
                                                        "         sortOrder: 0, \n" +
                                                        "         children:[ ], \n" +
                                                        "         folderType: \"Targeted MS\", \n" +
                                                        "         type: \"folder\", \n" +
                                                        "         activeModules: [\"Core\", \"Wiki\", \"TargetedMS\"], \n" +
                                                        "         effectivePermissions: [ \n" +  // WRITABLE
                                                        "             \"org.labkey.api.security.permissions.ReadPermission\", \n" +
                                                        "             \"org.labkey.api.security.permissions.InsertPermission\" \n" +
                                                        "          ], \n" +
                                                        "         name: \"user1\", \n" +
                                                        "         path: \"/MacCoss/user1\"\n" +
                                                        "         }\n" +
                                                        "     ], \n" +
                                                        "     folderType: \"Collaboration\", \n" +
                                                        "     type: \"project\", \n" +
                                                        "     activeModules: [\"Core\", \"Wiki\"], \n" +
                                                        "     effectivePermissions: [ \n" +  // READABLE
                                                        "         \"org.labkey.api.security.permissions.ReadPermission\" \n" +
                                                        "     ], \n" +
                                                        "     name: \"MacCoss\", \n" +
                                                        "     path: \"/MacCoss\"\n" +
                                                        "     } \n" +
                                                        "], \n" +
                                                        "folderType: \"None\", \n" +
                                                        "type: \"folder\", \n" +
                                                        "activeModules: [ ], \n" +
                                                        "title: \"\", \n" +
                                                        "effectivePermissions: [ ], \n" +
                                                        "name: \"\", \n" +
                                                        "path: \"/\", \n" +
                                                        "parentPath: \"null\" \n" +
                                                        "}";

        private const string WRITABLEFOLDER_2_JSON = "{sortOrder: 0, children: [\n" +
                                                        "    {\n" +
                                                        "     sortOrder: 0, \n" +
                                                        "     children:[\n" +
                                                        "         {\n" +
                                                        "         sortOrder: 0, \n" +
                                                        "         children:[ ], \n" +
                                                        "         folderType: \"Collaboration\", \n" +
                                                        "         type: \"folder\", \n" +
                                                        "         activeModules: [\"Core\", \"Wiki\"], \n" + // No TargetedMS module
                                                        "         effectivePermissions: [ \n" +  // WRITABLE
                                                        "             \"org.labkey.api.security.permissions.ReadPermission\", \n" +
                                                        "             \"org.labkey.api.security.permissions.InsertPermission\" \n" +
                                                        "          ], \n" +
                                                        "         name: \"user1\", \n" +
                                                        "         path: \"/Carr/user1\"\n" +
                                                        "         }\n" +
                                                        "     ], \n" +
                                                        "     folderType: \"Collaboration\", \n" +
                                                        "     type: \"project\", \n" +
                                                        "     activeModules: [\"Core\", \"Wiki\"], \n" +
                                                        "     effectivePermissions: [ \n" +  // READABLE
                                                        "         \"org.labkey.api.security.permissions.ReadPermission\" \n" +
                                                        "     ], \n" +
                                                        "     name: \"Carr\", \n" +
                                                        "     path: \"/Carr\"\n" +
                                                        "     }, \n" +
                                                        "    {\n" +
                                                        "     sortOrder: 0, \n" +
                                                        "     children:[\n" +
                                                        "         {\n" +
                                                        "         sortOrder: 0, \n" +
                                                        "         children:[ ], \n" +
                                                        "         folderType: \"Targeted MS\", \n" +
                                                        "         type: \"folder\", \n" +
                                                        "         activeModules: [\"Core\", \"Wiki\", \"TargetedMS\"], \n" +
                                                        "         effectivePermissions: [ \n" +  // WRITABLE
                                                        "             \"org.labkey.api.security.permissions.ReadPermission\", \n" +
                                                        "             \"org.labkey.api.security.permissions.InsertPermission\" \n" +
                                                        "          ], \n" +
                                                        "         name: \"user1\", \n" +
                                                        "         path: \"/MacCoss/user1\"\n" +
                                                        "         }\n" +
                                                        "     ], \n" +
                                                        "     folderType: \"Collaboration\", \n" +
                                                        "     type: \"project\", \n" +
                                                        "     activeModules: [\"Core\", \"Wiki\"], \n" +
                                                        "     effectivePermissions: [ ], \n" + // NO PERMISSIONS
                                                        "     name: \"MacCoss\", \n" +
                                                        "     path: \"/MacCoss\"\n" +
                                                        "     }, \n" +
                                                        "    {\n" +
                                                        "     sortOrder: 0, \n" +
                                                        "     children:[\n" +
                                                        "         {\n" +
                                                        "         sortOrder: 0, \n" +
                                                        "         children:[ ], \n" +
                                                        "         folderType: \"Targeted MS\", \n" +
                                                        "         type: \"folder\", \n" +
                                                        "         activeModules: [\"Core\", \"Wiki\", \"TargetedMS\"], \n" +
                                                        "         effectivePermissions: [ \n" +  // WRITABLE
                                                        "             \"org.labkey.api.security.permissions.ReadPermission\", \n" +
                                                        "             \"org.labkey.api.security.permissions.InsertPermission\" \n" +
                                                        "          ], \n" +
                                                        "         name: \"userFolder\", \n" +
                                                        "         path: \"/Gibson/userFolder\"\n" +
                                                        "         }\n" +
                                                        "     ], \n" +
                                                        "     folderType: \"Collaboration\", \n" +
                                                        "     type: \"project\", \n" +
                                                        "     activeModules: [\"Core\", \"Wiki\"], \n" +
                                                        "     effectivePermissions: [ \n" +  // READABLE
                                                        "         \"org.labkey.api.security.permissions.ReadPermission\" \n" +
                                                        "     ], \n" +
                                                        "     name: \"Gibson\", \n" +
                                                        "     path: \"/Gibson\"\n" +
                                                        "     }, \n" +
                                                        "], \n" +
                                                        "folderType: \"None\", \n" +
                                                        "type: \"folder\", \n" +
                                                        "activeModules: [ ], \n" +
                                                        "title: \"\", \n" +
                                                        "effectivePpermissions: [ ], \n" +
                                                        "name: \"\", \n" +
                                                        "path: \"/\", \n" +
                                                        "parentPath: \"null\" \n" +
                                                        "}";   
    }
}
           