"""Add loop_last to res_musiconhold

Revision ID: f5b0e7427449
Revises: f261363a857f
Create Date: 2023-03-13 23:59:00.835055

"""

# revision identifiers, used by Alembic.
revision = 'f5b0e7427449'
down_revision = 'f261363a857f'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)
    op.add_column('musiconhold', sa.Column('loop_last', yesno_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('musiconhold','loop_last')
    op.drop_column('musiconhold', 'loop_last')
