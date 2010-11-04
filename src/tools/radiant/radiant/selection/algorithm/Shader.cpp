#include "Shader.h"

#include "iregistry.h"
#include "iselection.h"
#include "iscenegraph.h"
#include "../../brush/FaceInstance.h"
#include "../../brush/BrushVisit.h"
#include "../../brush/TextureProjection.h"
#include "Primitives.h"

// greebo: Nasty global that contains all the selected face instances
extern FaceInstanceSet g_SelectedFaceInstances;

namespace selection {
	namespace algorithm {

// Constants
namespace {
	const std::string RKEY_DEFAULT_TEXTURE_SCALE = "user/ui/textures/defaultTextureScale";
}

class AmbiguousShaderException:
	public std::runtime_error
{
public:
	// Constructor
	AmbiguousShaderException(const std::string& what):
		std::runtime_error(what)
	{}
};

/** greebo: Cycles through all the Faces and throws as soon as
 * at least two different non-empty shader names are found.
 *
 * @throws: AmbiguousShaderException
 */
class UniqueFaceShaderFinder
{
	// The string containing the result
	mutable std::string& _shader;

public:
	UniqueFaceShaderFinder(std::string& shader) :
		_shader(shader)
	{}

	void operator()(FaceInstance& face) const {

		std::string foundShader = face.getFace().GetShader();

		if (foundShader != "$NONE" && _shader != "$NONE" &&
			_shader != foundShader)
		{
			throw AmbiguousShaderException(foundShader);
		}

		_shader = foundShader;
	}
};

std::string getShaderFromSelection() {
	std::string returnValue("");

	const SelectionInfo& selectionInfo = GlobalSelectionSystem().getSelectionInfo();

	if (selectionInfo.totalCount > 0) {
		std::string faceShader("$NONE");

		// BRUSHES
		// If there are no FaceInstances selected, cycle through the brushes
		if (g_SelectedFaceInstances.empty()) {
			// Try to get the unique shader from the selected brushes
			try {
				// Go through all the selected brushes and their faces
				Scene_ForEachSelectedBrush_ForEachFaceInstance(
					GlobalSceneGraph(),
					UniqueFaceShaderFinder(faceShader)
				);
			}
			catch (AmbiguousShaderException a) {
				faceShader = "";
			}
		}
		else {
			// Try to get the unique shader from the faces
			try {
				g_SelectedFaceInstances.foreach(UniqueFaceShaderFinder(faceShader));
			}
			catch (AmbiguousShaderException a) {
				faceShader = "";
			}
		}

		if (faceShader != "$NONE") {
			// Only a faceShader has been found
			returnValue = faceShader;
		}
	}

	return returnValue;
}

TextureProjection getSelectedTextureProjection() {
	TextureProjection returnValue;

	if (selectedFaceCount() == 1) {
		// Get the last selected face instance from the global
		FaceInstance& faceInstance = g_SelectedFaceInstances.last();
		faceInstance.getFace().GetTexdef(returnValue);
	}

	return returnValue;
}

Vector2 getSelectedFaceShaderSize() {
	Vector2 returnValue(0,0);

	if (selectedFaceCount() == 1) {
		// Get the last selected face instance from the global
		FaceInstance& faceInstance = g_SelectedFaceInstances.last();

		returnValue[0] = faceInstance.getFace().getShader().width();
		returnValue[1] = faceInstance.getFace().getShader().height();
	}

	return returnValue;
}

/** greebo: Applies the given texture repeat to the visited face
 */
class FaceTextureFitter
{
	float _repeatS, _repeatT;
public:
	FaceTextureFitter(float repeatS, float repeatT) :
		_repeatS(repeatS), _repeatT(repeatT)
	{}

	void operator()(Face& face) const {
		face.FitTexture(_repeatS, _repeatT);
	}
};

void fitTexture(const float& repeatS, const float& repeatT) {
	UndoableCommand command("fitTexture");

	// Cycle through all selected brushes
	if (GlobalSelectionSystem().Mode() != SelectionSystem::eComponent) {
		Scene_ForEachSelectedBrush_ForEachFace(GlobalSceneGraph(), FaceTextureFitter(repeatS, repeatT));
	}

	// Cycle through all selected components
	Scene_ForEachSelectedBrushFace(FaceTextureFitter(repeatS, repeatT));

	SceneChangeNotify();
}

/** greebo: Applies the default texture projection to all
 * the visited faces.
 */
class FaceTextureProjectionSetter
{
	TextureProjection& _projection;
public:
	FaceTextureProjectionSetter(TextureProjection& projection) :
		_projection(projection)
	{}

	void operator()(Face& face) const {
		face.SetTexdef(_projection);
	}
};

void naturalTexture() {
	UndoableCommand undo("naturalTexture");

	TextureProjection projection;
	projection.constructDefault();

	// Faces
	Scene_ForEachSelectedBrushFace(FaceTextureProjectionSetter(projection));

	SceneChangeNotify();
}

void applyTextureProjectionToFaces(TextureProjection& projection) {
	UndoableCommand undo("textureProjectionSetSelected");

	if (GlobalSelectionSystem().Mode() != SelectionSystem::eComponent) {
		Scene_ForEachSelectedBrush_ForEachFace(GlobalSceneGraph(), FaceTextureProjectionSetter(projection));
	}

	// Faces
	Scene_ForEachSelectedBrushFace(FaceTextureProjectionSetter(projection));

	SceneChangeNotify();
}

/** greebo: Translates the texture of the visited faces
 * about the specified <shift> Vector2
 */
class FaceTextureShifter
{
	const Vector2& _shift;
public:
	FaceTextureShifter(const Vector2& shift) :
		_shift(shift)
	{}

	void operator()(Face& face) const {
		face.ShiftTexdef(_shift[0], _shift[1]);
	}
};

void shiftTexture(const Vector2& shift) {
	std::string command("shiftTexture: ");
	command += "s=" + string::toString(shift[0]) + ", t=" + string::toString(shift[1]);

	UndoableCommand undo(command);

	if (GlobalSelectionSystem().Mode() != SelectionSystem::eComponent) {
		Scene_ForEachSelectedBrush_ForEachFace(GlobalSceneGraph(), FaceTextureShifter(shift));
	}
	// Translate the face textures
	Scene_ForEachSelectedBrushFace(FaceTextureShifter(shift));

	SceneChangeNotify();
}

/** greebo: Scales the texture of the visited faces
 * about the specified x,y-scale values in the given Vector2
 */
class FaceTextureScaler
{
	const Vector2& _scale;
public:
	FaceTextureScaler(const Vector2& scale) :
		_scale(scale)
	{}

	void operator()(Face& face) const {
		face.ScaleTexdef(_scale[0], _scale[1]);
	}
};

/** greebo: Rotates the texture of the visited faces
 * about the specified angle
 */
class FaceTextureRotater
{
	const float& _angle;
public:
	FaceTextureRotater(const float& angle) :
		_angle(angle)
	{}

	void operator()(Face& face) const {
		face.RotateTexdef(_angle);
	}
};

void scaleTexture(const Vector2& scale) {
	std::string command("scaleTexture: ");
	command += "sScale=" + string::toString(scale[0]) + ", tScale=" + string::toString(scale[1]);

	UndoableCommand undo(command);

	if (GlobalSelectionSystem().Mode() != SelectionSystem::eComponent) {
		Scene_ForEachSelectedBrush_ForEachFace(
			GlobalSceneGraph(),
			FaceTextureScaler(scale)
		);
	}
	// Scale the face textures
	Scene_ForEachSelectedBrushFace(FaceTextureScaler(scale));

	SceneChangeNotify();
}

void rotateTexture(const float& angle) {
	std::string command("rotateTexture: ");
	command += "angle=" + string::toString(angle);

	UndoableCommand undo(command.c_str());

	if (GlobalSelectionSystem().Mode() != SelectionSystem::eComponent) {
		Scene_ForEachSelectedBrush_ForEachFace(
			GlobalSceneGraph(),
			FaceTextureRotater(angle)
		);
	}

	// Rotate the face textures
	Scene_ForEachSelectedBrushFace(FaceTextureRotater(angle));

	SceneChangeNotify();
}

void shiftTextureLeft() {
	shiftTexture(Vector2(-GlobalRegistry().getFloat("user/ui/textures/surfaceInspector/hShiftStep"), 0.0f));
}

void shiftTextureRight() {
	shiftTexture(Vector2(GlobalRegistry().getFloat("user/ui/textures/surfaceInspector/hShiftStep"), 0.0f));
}

void shiftTextureUp() {
	shiftTexture(Vector2(0.0f, GlobalRegistry().getFloat("user/ui/textures/surfaceInspector/vShiftStep")));
}

void shiftTextureDown() {
	shiftTexture(Vector2(0.0f, -GlobalRegistry().getFloat("user/ui/textures/surfaceInspector/vShiftStep")));
}

void scaleTextureLeft() {
	scaleTexture(Vector2(-GlobalRegistry().getFloat("user/ui/textures/surfaceInspector/hScaleStep"), 0.0f));
}

void scaleTextureRight() {
	scaleTexture(Vector2(GlobalRegistry().getFloat("user/ui/textures/surfaceInspector/hScaleStep"), 0.0f));
}

void scaleTextureUp() {
	scaleTexture(Vector2(0.0f, GlobalRegistry().getFloat("user/ui/textures/surfaceInspector/vScaleStep")));
}

void scaleTextureDown() {
	scaleTexture(Vector2(0.0f, -GlobalRegistry().getFloat("user/ui/textures/surfaceInspector/vScaleStep")));
}

void rotateTextureClock() {
	rotateTexture(fabs(GlobalRegistry().getFloat("user/ui/textures/surfaceInspector/rotStep")));
}

void rotateTextureCounter() {
	rotateTexture(-fabs(GlobalRegistry().getFloat("user/ui/textures/surfaceInspector/rotStep")));
}

	} // namespace algorithm
} // namespace selection
