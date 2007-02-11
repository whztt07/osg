/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield
 *
 * This library is open source and may be redistributed and/or modified under
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * OpenSceneGraph Public License for more details.
*/
//osgManipulator - Copyright (C) 2007 Fugro-Jason B.V.

#include <osgManipulator/Scale1DDragger>
#include <osgManipulator/Command>
#include <osgManipulator/CommandManager>

#include <osg/ShapeDrawable>
#include <osg/Geometry>
#include <osg/LineWidth>
#include <osg/Material>

using namespace osgManipulator;

namespace
{

float computeScale(const osg::Vec3& startProjectedPoint,
                   const osg::Vec3& projectedPoint, float scaleCenter)
{
    float denom = startProjectedPoint[0] - scaleCenter;
    float scale = denom ? (projectedPoint[0] - scaleCenter)/denom : 1.0;
    return scale;
}

}

Scale1DDragger::Scale1DDragger(ScaleMode scaleMode) : Dragger(), _minScale(0.001), _scaleMode(scaleMode)
{
    _projector = new LineProjector(osg::Vec3(-0.5f,0.0f,0.0f),osg::Vec3(0.5f,0.0f,0.0f));
    setColor(osg::Vec4(0.0f, 1.0f, 0.0f, 1.0f));
    setPickColor(osg::Vec4(1.0f, 1.0f, 0.0f, 1.0f));
}

Scale1DDragger::~Scale1DDragger()
{
}

bool Scale1DDragger::handle(int pixel_x, int pixel_y, const osgUtil::SceneView& sv, 
        const osgUtil::IntersectVisitor::HitList&, const osgUtil::IntersectVisitor::HitList::iterator& hitIter,
        const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
{
    // Check if the dragger node is in the nodepath.
    if (std::find((*hitIter)._nodePath.begin(), (*hitIter)._nodePath.end(), this) == (*hitIter)._nodePath.end())
        return false;

    switch (ea.getEventType())
    {
        // Pick start.
        case (osgGA::GUIEventAdapter::PUSH):
            {
                // Get the LocalToWorld matrix for this node and set it for the projector.
                osg::NodePath nodePathToRoot;
                computeNodePathToRoot(*this,nodePathToRoot);
                osg::Matrix localToWorld = osg::computeLocalToWorld(nodePathToRoot);
                _projector->setLocalToWorld(localToWorld);

                if (_projector->project(osg::Vec2(pixel_x, pixel_y), sv, _startProjectedPoint))
                {
                    _scaleCenter = 0.0f;
                    if (_scaleMode == SCALE_WITH_OPPOSITE_HANDLE_AS_PIVOT)
                    {
                        if (std::find((*hitIter)._nodePath.begin(), (*hitIter)._nodePath.end(), _leftHandleNode.get()) 
                                != (*hitIter)._nodePath.end())
                            _scaleCenter = _projector->getLineEnd()[0];
                        else if (std::find((*hitIter)._nodePath.begin(), (*hitIter)._nodePath.end(), _rightHandleNode.get()) 
                                != (*hitIter)._nodePath.end())
                            _scaleCenter = _projector->getLineStart()[0];
                    }

                    // Generate the motion command.
                    osg::ref_ptr<Scale1DCommand> cmd = new Scale1DCommand();
                    cmd->setStage(MotionCommand::START);
                    cmd->setLocalToWorldAndWorldToLocal(_projector->getLocalToWorld(),_projector->getWorldToLocal());

                    // Dispatch command.
                    if (_commandManager)
                    {
                        _commandManager->addSelectionsToCommand(*cmd, *getParentDragger());
                        _commandManager->dispatch(*cmd);
                    }

                    // Set color to pick color.
                    setMaterialColor(_pickColor,*this);

                    aa.requestRedraw();
                }
                return true; 
            }
            
        // Pick move.
        case (osgGA::GUIEventAdapter::DRAG):
            {
                osg::Vec3 projectedPoint;
                if (_projector->project(osg::Vec2(pixel_x, pixel_y), sv, projectedPoint))
                {
                    // Generate the motion command.
                    osg::ref_ptr<Scale1DCommand> cmd = new Scale1DCommand();

                    // Compute scale.
                    float scale = computeScale(_startProjectedPoint,projectedPoint,_scaleCenter);
                    if (scale < getMinScale()) scale = getMinScale();

                    // Snap the referencePoint to the line start or line end depending on which is closer.
                    float referencePoint = _startProjectedPoint[0];
                    if (fabs(_projector->getLineStart()[0] - referencePoint) <
                        fabs(_projector->getLineEnd()[0]   - referencePoint))
                        referencePoint = _projector->getLineStart()[0];
                    else
                        referencePoint = _projector->getLineEnd()[0];
                    
                    cmd->setStage(MotionCommand::MOVE);
                    cmd->setLocalToWorldAndWorldToLocal(_projector->getLocalToWorld(),_projector->getWorldToLocal());
                    cmd->setScale(scale);
                    cmd->setScaleCenter(_scaleCenter);
                    cmd->setReferencePoint(referencePoint);
                    cmd->setMinScale(getMinScale());

                    // Dispatch command.
                    if (_commandManager)
                    {
                        _commandManager->addSelectionsToCommand(*cmd, *getParentDragger());
                        _commandManager->dispatch(*cmd);
                    }

                    aa.requestRedraw();
                }
                return true; 
            }
            
        // Pick finish.
        case (osgGA::GUIEventAdapter::RELEASE):
            {
                osg::ref_ptr<Scale1DCommand> cmd = new Scale1DCommand();

                cmd->setStage(MotionCommand::FINISH);
                cmd->setLocalToWorldAndWorldToLocal(_projector->getLocalToWorld(),_projector->getWorldToLocal());
	    	
                // Dispatch command.
                if (_commandManager)
                {
                    _commandManager->addSelectionsToCommand(*cmd, *getParentDragger());
                    _commandManager->dispatch(*cmd);
                }

                // Reset color.
                setMaterialColor(_color,*this);
		
                aa.requestRedraw();

                return true;
            }
        default:
            return false;
    }
    return false;
}

void Scale1DDragger::setupDefaultGeometry()
{
    // Get the line length and direction.
    osg::Vec3 lineDir = _projector->getLineEnd()-_projector->getLineStart();
    float lineLength = lineDir.length();
    lineDir.normalize();

    osg::Geode* lineGeode = new osg::Geode;
    // Create a line.
    {
        osg::Geometry* geometry = new osg::Geometry();
        
        osg::Vec3Array* vertices = new osg::Vec3Array(2);
        (*vertices)[0] = _projector->getLineStart();
        (*vertices)[1] = _projector->getLineEnd();

        geometry->setVertexArray(vertices);
        geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES,0,2));

        lineGeode->addDrawable(geometry);
    }
    
    // Turn of lighting for line and set line width.
    lineGeode->getOrCreateStateSet()->setMode(GL_LIGHTING,osg::StateAttribute::OFF);
    osg::LineWidth* linewidth = new osg::LineWidth();
    linewidth->setWidth(2.0f);
    lineGeode->getOrCreateStateSet()->setAttributeAndModes(linewidth, osg::StateAttribute::ON);

    // Add line and cones to the scene.
    addChild(lineGeode);

    // Create a left box.
    {
        osg::Geode* geode = new osg::Geode;
        geode->addDrawable(new osg::ShapeDrawable(new osg::Box(_projector->getLineStart(), 0.05 * lineLength)));
        addChild(geode);
        setLeftHandleNode(*geode);
    }
    
    // Create a right box.
    {
        osg::Geode* geode = new osg::Geode;
        geode->addDrawable(new osg::ShapeDrawable(new osg::Box(_projector->getLineEnd(), 0.05 * lineLength)));
        addChild(geode);
        setRightHandleNode(*geode);
    }
}
