void ComponentRemovalTransactions() {
    Fixture f;
    f.session.GetReflectionRegistration().RegisterInspector(f.types);
    const auto first = Id(f.session.GetLevel()->CreateEntity("First"));
    const auto second = Id(f.session.GetLevel()->CreateEntity("Second"));
    f.session.Selection().Replace(std::array{first, second});
    const auto* camera = f.types.Find("Camera");
    assert(camera && camera->remove);
    assert(!f.types.Find("Name")->remove && !f.types.Find("Transform")->remove);
    assert(f.edits.AddComponent(*camera));
    const auto enabled = PropertyHandle{first, "Camera", "enabled"};
    assert(f.properties.Preview(enabled, false));
    f.properties.Commit();
    assert(f.edits.RemoveComponent(*camera));
    assert(!camera->contains(f.session.GetLevel()->FindEntity(first)) && !camera->contains(f.session.GetLevel()->FindEntity(second)));
    assert(f.properties.Undo());
    assert(camera->contains(f.session.GetLevel()->FindEntity(first)) && camera->contains(f.session.GetLevel()->FindEntity(second)));
    assert(!f.session.GetLevel()->FindEntity(first).GetComponent<CameraComponent>()->enabled);
    auto rejecting = *camera;
    const auto remove = rejecting.remove;
    rejecting.remove = [remove, second](Entity entity) {
        if(entity.GetComponent<PersistentIdComponent>()->value == second) {
            return false;
        }
        return remove(entity);
    };
    assert(!f.edits.RemoveComponent(rejecting));
    assert(camera->contains(f.session.GetLevel()->FindEntity(first)) && camera->contains(f.session.GetLevel()->FindEntity(second)));
    f.session.Selection().Replace(std::array{first});
    assert(f.edits.RemoveComponent(*camera));
    assert(!camera->contains(f.session.GetLevel()->FindEntity(first)) && camera->contains(f.session.GetLevel()->FindEntity(second)));
    f.session.Selection().Replace(std::array{first, second});
    assert(f.edits.RemoveComponent(*camera));
    assert(f.properties.Undo() && camera->contains(f.session.GetLevel()->FindEntity(second)));
    assert(!camera->contains(f.session.GetLevel()->FindEntity(first)));
    assert(f.session.StartPlay());
    assert(!f.edits.RemoveComponent(*camera));
    f.session.StopPlay();
    std::cout << "PASS: component removal multi-selection, missing targets, values/undo/redo, rollback, protected types and Play guard\n";
}
