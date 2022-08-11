import { ComponentFixture, TestBed } from "@angular/core/testing";

import { ViewPaneComponent } from "./view-pane.component";

describe("ViewPaneComponent", () => {
  let component: ViewPaneComponent;
  let fixture: ComponentFixture<ViewPaneComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ViewPaneComponent],
    }).compileComponents();

    fixture = TestBed.createComponent(ViewPaneComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it("should create", () => {
    expect(component).toBeTruthy();
  });
});
