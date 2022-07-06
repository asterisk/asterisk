"""max_random_initial_delay

Revision ID: 18e0805d367f
Revises: 0bee61aa9425
Create Date: 2022-05-18 17:07:02.626045

"""

# revision identifiers, used by Alembic.
revision = '18e0805d367f'
down_revision = '0bee61aa9425'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_registrations', sa.Column('max_random_initial_delay', sa.Integer))

def downgrade():
    op.drop_column('ps_registrations', 'max_random_initial_delay')
