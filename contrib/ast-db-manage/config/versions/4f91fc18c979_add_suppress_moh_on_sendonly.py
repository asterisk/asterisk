"""Add suppress_moh_on_sendonly

Revision ID: 4f91fc18c979
Revises: 801b9fced8b7
Create Date: 2024-11-05 11:37:33.604448

"""

# revision identifiers, used by Alembic.
revision = '4f91fc18c979'
down_revision = '801b9fced8b7'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']


def upgrade():
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    op.add_column('ps_endpoints', sa.Column('suppress_moh_on_sendonly', yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_suppress_moh_on_sendonly_yesno_values', 'ps_endpoints')
    op.drop_column('ps_endpoints', 'suppress_moh_on_sendonly')
